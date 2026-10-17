// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// FaceModel.h - key hit-testing on the photo faces: a port of PhotoFace.hitRect / key(at:) from the
// Mac app, so a click lands on the same key on both. Pure C++ (no Qt types), unit-tested.
#pragma once
#include "FaceGeometry.h"
#include <algorithm>
#include <vector>

namespace vc {

class FaceModel {
public:
    explicit FaceModel(const FaceGeometry &g) : g_(g) {
        std::vector<double> dx, dy;
        for (int code = 0; code < 40; ++code) {
            if (!g_.hasKey[code] || code == 25) continue;
            if (code % 10 != 9 && code + 1 != 25 && g_.hasKey[code + 1]) dx.push_back(midX(code + 1) - midX(code));
            if (code + 10 < 40 && code + 10 != 25 && code + 10 != 35 && g_.hasKey[code + 10])
                dy.push_back(g_.key[code + 10].y - g_.key[code].y);
        }
        pitchW_ = median(dx); pitchH_ = median(dy);
    }
    const FaceGeometry &geometry() const { return g_; }
    double aspect() const { return double(g_.pixelW) / g_.pixelH; }
    bool hasKey(int code) const { return code >= 0 && code < 40 && g_.hasKey[code == 35 ? 25 : code]; }
    Rect4 keyRect(int code) const { return g_.key[code == 35 ? 25 : code]; }

    /// The area answering a click for key `code` (fractions of the face): the cap grown half-way into the
    /// gaps beside it and upwards over its gold legend.
    Rect4 hitRect(int code) const {
        int c = code == 35 ? 25 : code;
        Rect4 r = g_.key[c];
        auto at = [&](int n, Rect4 *out) {
            if (n < 0 || n >= 40) return false;
            int m = n == 35 ? 25 : n;
            if (m == c || !g_.hasKey[m]) return false;
            *out = g_.key[m]; return true;
        };
        int col = c % 10;
        double gapX = std::max(0.0, pitchW_ - r.w);
        double capH = g_.hasKey[24] ? g_.key[24].h : r.h;
        double gapY = std::max(0.0, pitchH_ - capH);
        const double up = 0.68;
        Rect4 o{};
        double left = r.x - gapX / 2, right = r.x + r.w + gapX / 2, top = r.y - gapY * up, bottom = r.y + r.h + gapY * (1 - up);
        if (col > 0 && at(c - 1, &o)) left = ((o.x + o.w) + r.x) / 2;
        if (col < 9 && at(c + 1, &o)) right = ((r.x + r.w) + o.x) / 2;
        if (at(c - 10, &o)) top = (o.y + o.h) + (r.y - (o.y + o.h)) * (1 - up);
        if (at(c + 10, &o)) bottom = (r.y + r.h) + (o.y - (r.y + r.h)) * (1 - up);
        else if (at(c + 20, &o)) bottom = (r.y + r.h) + (o.y - (r.y + r.h)) * (1 - up);
        left = std::max(0.0, left); top = std::max(0.0, top);
        return {left, top, std::min(1.0, right) - left, std::min(1.0, bottom) - top};
    }

    /// Key code at a point given as fractions of the face, or -1 between the keys' areas.
    int keyAt(double px, double py) const {
        for (int c = 0; c < 40; ++c)
            if (g_.hasKey[c] && contains(g_.key[c], px, py)) return c;
        for (int c = 0; c < 40; ++c)
            if (g_.hasKey[c] && contains(hitRect(c), px, py)) return c;
        return -1;
    }

private:
    static bool contains(const Rect4 &r, double x, double y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }
    double midX(int c) const { return g_.key[c].x + g_.key[c].w / 2; }
    static double median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v.empty() ? 0 : v[v.size() / 2]; }
    const FaceGeometry &g_;
    double pitchW_ = 0, pitchH_ = 0;
};

}  // namespace vc
