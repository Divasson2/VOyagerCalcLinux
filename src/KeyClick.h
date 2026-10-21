// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// KeyClick.h - the optional deep key-click sound (View ▸ Key Click Sound, off by default; 0.7.1).
// The sound is synthesised (a short low "thock": a falling 170 -> 85 Hz tone with a soft attack transient),
// so no audio file and no Qt Multimedia are needed. Playback: Windows PlaySound (winmm, in memory, async);
// Linux the PulseAudio "simple" API (also served by PipeWire) or ALSA, both loaded at run time with dlopen,
// so the app still starts on a system without them (the click is then silent). Playback runs on its own thread.
#pragma once
#include <QByteArray>
#include <cstdint>
#include <vector>

namespace vc {

class KeyClick {
public:
    static constexpr int kRate = 44100;
    /// The click as 16-bit mono samples at `rate` (pure function, unit-tested).
    static std::vector<int16_t> samples(int rate = kRate);
    /// The click as a complete RIFF/WAVE file (Windows plays it from memory).
    static QByteArray wav();
    /// Play once, asynchronously. Never blocks the caller; does nothing when no audio output is available.
    static void play();
    /// Which output the click uses on this system: "PlaySound", "PulseAudio", "ALSA" or "" (none found yet).
    static QString backend();
    /// Tests: count of play() calls (whether or not a sound device exists).
    static int playCount();
};

}  // namespace vc
