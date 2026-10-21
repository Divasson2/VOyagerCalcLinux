// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "KeyClick.h"
#include <QString>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>
#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#else
#include <dlfcn.h>
#endif

namespace vc {

static std::atomic<int> gPlays{0};

std::vector<int16_t> KeyClick::samples(int rate) {
    const double len = 0.050;                          // 50 ms
    const int n = int(rate * len);
    std::vector<int16_t> out(size_t(n), 0);
    double phase = 0;
    uint32_t noise = 0x1234567u;
    double lp = 0;
    for (int i = 0; i < n; ++i) {
        const double t = double(i) / rate;
        const double f = 85.0 + 85.0 * std::exp(-t / 0.012);           // 170 Hz falling to 85 Hz: the deep "thock"
        phase += 2 * M_PI * f / rate;
        const double body = std::sin(phase) * std::exp(-t / 0.014) * (1 - std::exp(-t / 0.0006));
        noise = noise * 1664525u + 1013904223u;
        const double white = (double(noise >> 8) / double(1 << 24)) * 2 - 1;
        lp += (white - lp) * 0.18;                                       // soft (low-passed) attack transient
        const double tick = lp * std::exp(-t / 0.0015) * 0.9;
        double v = 0.62 * body + 0.30 * tick;
        if (i > n - rate / 200) v *= double(n - i) / (rate / 200);       // 5 ms fade-out: no click at the end
        out[size_t(i)] = int16_t(std::lround(std::clamp(v, -1.0, 1.0) * 32000));
    }
    return out;
}

QByteArray KeyClick::wav() {
    const auto s = samples(kRate);
    const uint32_t data = uint32_t(s.size() * 2);
    QByteArray w;
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) w.append(char(v >> (8 * i) & 0xFF)); };
    auto u16 = [&](uint16_t v) { w.append(char(v & 0xFF)); w.append(char(v >> 8)); };
    w.append("RIFF"); u32(36 + data); w.append("WAVE");
    w.append("fmt "); u32(16); u16(1); u16(1); u32(kRate); u32(kRate * 2); u16(2); u16(16);
    w.append("data"); u32(data);
    w.append(reinterpret_cast<const char *>(s.data()), int(data));
    return w;
}

int KeyClick::playCount() { return gPlays.load(); }

#ifdef Q_OS_WIN
QString KeyClick::backend() { return QStringLiteral("PlaySound"); }
void KeyClick::play() {
    ++gPlays;
    static const QByteArray w = wav();                 // must stay alive while it plays (SND_ASYNC)
    PlaySoundW(reinterpret_cast<LPCWSTR>(w.constData()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
#else
namespace {
// the few PulseAudio / ALSA entry points used, resolved with dlsym
struct PaSpec { int format; uint32_t rate; uint8_t channels; };
struct PaAttr { uint32_t maxlength, tlength, prebuf, minreq, fragsize; };
using PaNew = void *(*)(const char *, const char *, int, const char *, const char *, const PaSpec *, const void *, const PaAttr *, int *);
using PaWrite = int (*)(void *, const void *, size_t, int *);
using AlsaOpen = int (*)(void **, const char *, int, int);
using AlsaSet = int (*)(void *, int, int, unsigned, unsigned, int, unsigned);
using AlsaWrite = long (*)(void *, const void *, unsigned long);
using AlsaPrepare = int (*)(void *);

class Player {
public:
    static Player &get() { static Player *p = new Player; return *p; }   // never destroyed: the detached thread waits on cv_ until exit
    void trigger() { { std::lock_guard<std::mutex> l(m_); ++pending_; } cv_.notify_one(); }
    QString name() { std::lock_guard<std::mutex> l(m_); return name_; }
private:
    Player() : th_([this] { loop(); }) { th_.detach(); }
    bool open() {
        if (pa_ || alsa_) return true;
        if (tried_) return false;
        tried_ = true;
        if (void *lib = dlopen("libpulse-simple.so.0", RTLD_NOW | RTLD_LOCAL)) {
            auto mk = reinterpret_cast<PaNew>(dlsym(lib, "pa_simple_new"));
            paWrite_ = reinterpret_cast<PaWrite>(dlsym(lib, "pa_simple_write"));
            if (mk && paWrite_) {
                PaSpec spec{3 /* PA_SAMPLE_S16LE */, uint32_t(KeyClick::kRate), 1};
                const uint32_t ms20 = uint32_t(KeyClick::kRate / 50 * 2);
                PaAttr attr{uint32_t(-1), ms20, uint32_t(-1), uint32_t(-1), uint32_t(-1)};
                int err = 0;
                pa_ = mk(nullptr, "VoyagerCalc", 1 /* PA_STREAM_PLAYBACK */, nullptr, "key click", &spec, nullptr, &attr, &err);
                if (pa_) { std::lock_guard<std::mutex> l(m_); name_ = QStringLiteral("PulseAudio"); return true; }
            }
        }
        if (void *lib = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL)) {
            auto op = reinterpret_cast<AlsaOpen>(dlsym(lib, "snd_pcm_open"));
            auto set = reinterpret_cast<AlsaSet>(dlsym(lib, "snd_pcm_set_params"));
            alsaWrite_ = reinterpret_cast<AlsaWrite>(dlsym(lib, "snd_pcm_writei"));
            alsaPrepare_ = reinterpret_cast<AlsaPrepare>(dlsym(lib, "snd_pcm_prepare"));
            void *h = nullptr;
            if (op && set && alsaWrite_ && alsaPrepare_ && op(&h, "default", 0 /* PLAYBACK */, 0) == 0 &&
                set(h, 2 /* S16_LE */, 3 /* RW_INTERLEAVED */, 1, unsigned(KeyClick::kRate), 1, 30000) == 0) {
                alsa_ = h; std::lock_guard<std::mutex> l(m_); name_ = QStringLiteral("ALSA"); return true;
            }
        }
        return false;
    }
    void loop() {
        const std::vector<int16_t> s = KeyClick::samples();
        for (;;) {
            { std::unique_lock<std::mutex> l(m_); cv_.wait(l, [this] { return pending_ > 0; }); pending_ = 0; }
            if (!open()) continue;
            if (pa_) { int err = 0; paWrite_(pa_, s.data(), s.size() * 2, &err); }
            else if (alsa_) {
                long r = alsaWrite_(alsa_, s.data(), s.size());
                if (r < 0) { alsaPrepare_(alsa_); alsaWrite_(alsa_, s.data(), s.size()); }
            }
        }
    }
    std::mutex m_;
    std::condition_variable cv_;
    int pending_ = 0;
    bool tried_ = false;
    void *pa_ = nullptr, *alsa_ = nullptr;
    PaWrite paWrite_ = nullptr;
    AlsaWrite alsaWrite_ = nullptr;
    AlsaPrepare alsaPrepare_ = nullptr;
    QString name_;
    std::thread th_;
};
}  // namespace

QString KeyClick::backend() { return Player::get().name(); }
void KeyClick::play() { ++gPlays; Player::get().trigger(); }
#endif

}  // namespace vc
