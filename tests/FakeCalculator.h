// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// A fake HP CE in HID mode: command byte >= 0x40, otherwise [n, n bytes] into a queue that the 'C' handler takes;
// 0xFE answers with the 2 KB memory in [n, n bytes] reports (parses the reports like the CE firmware, ANALYSIS.md 2).
#pragma once
#include "usb/HidTransport.h"
#include "usb/CalculatorLink.h"
#include <QThread>
#include <set>
#include <vector>

namespace vc {

class FakeCalculator : public HidTransport {
public:
    QByteArray memory;
    bool connected = true, silent = false, corruptLength = false, stopsOnCPUStateChange = false;
    int chunk = 63;
    int stopAfter = -1;                       // stop answering after this many dump bytes
    std::set<int> ignoreBlocks, ignoreOnceBlocks;
    std::vector<Report> sent;
    int opens = 0, closes = 0, writesSeen = 0;

    explicit FakeCalculator(QByteArray m) : memory(std::move(m)) {}
    void open() override { if (!connected) throw LinkError::noDevice(); ++opens; }
    void close() override { ++closes; }
    void send(const Report &r) override {
        sent.push_back(r);
        if (!connected) throw LinkError::io("bad argument (0xE00002C2)");
        if (r[0] >= 0x40) { afterDump_ = true; command(r[0]); return; }
        for (int i = 1; i <= r[0]; ++i) queue_.append(char(r[i]));
        while (!queue_.isEmpty()) {
            uchar c = uchar(queue_[0]);
            if (c == 0x43) {
                if (queue_.size() < 34) return;
                int block = uchar(queue_[1]);
                QByteArray data = queue_.mid(2, 32);
                queue_.remove(0, 34);
                if (afterDump_) { ++writesSeen; afterDump_ = false; }
                if (stopsOnCPUStateChange && block * 32 < 0x70) {
                    for (int i = 0; i < std::min(32, 0x70 - block * 32); ++i) {
                        int a = block * 32 + i; uchar mask = a == CalculatorLink::flag8Offset ? uchar(~CalculatorLink::flag8Mask) : 0xFF;
                        if ((uchar(memory[a]) & mask) != (uchar(data[i]) & mask)) { connected = false; return; }
                    }
                }
                if (ignoreBlocks.count(block) || (writesSeen == 1 && ignoreOnceBlocks.count(block))) continue;
                memory.replace(block * 32, 32, data);
            } else { queue_.remove(0, 1); afterDump_ = true; command(c); }
        }
    }
    bool receive(int timeoutMs, Report *out) override {
        if (outbox_.empty()) { QThread::msleep(std::min(timeoutMs, 5)); return false; }
        *out = outbox_.front(); outbox_.erase(outbox_.begin());
        return true;
    }
private:
    void command(uchar c) {
        if (c != 0xFE || silent) return;
        int pos = 0, end = std::min<int>(memory.size(), stopAfter < 0 ? memory.size() : stopAfter);
        while (pos < end) {
            int n = std::min(chunk, end - pos);
            Report r{}; r[0] = uint8_t(n);
            for (int i = 0; i < n; ++i) r[1 + i] = uint8_t(memory[pos + i]);
            outbox_.push_back(r); pos += n;
        }
        if (corruptLength) { Report bad{}; bad[0] = 0x40; outbox_.insert(outbox_.begin() + 1, bad); }
    }
    bool afterDump_ = true;
    QByteArray queue_;
    std::vector<Report> outbox_;
};


}  // namespace vc
