// Hostile params / transport / events must never produce NaN or Inf.
#include <random>

#include "TestUtil.hpp"

using namespace kikset;

int main() {
    const double fs = 48000.0;
    std::mt19937 rng(1234);
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    const double nasty[] = {std::nan(""), INFINITY, -INFINITY, 1e300, -1e300, 0.0, -0.0, 1e-300};

    KiksetEngine e;
    e.setSampleRate(fs);
    std::vector<float> l(256), r(256);
    bool finite = true;
    double peak = 0;
    for (int iter = 0; iter < 600; ++iter) {
        for (const auto& p : kParams) {
            const double pick = uni(rng);
            double v;
            if (pick < 0.15) v = nasty[rng() % 8];
            else if (pick < 0.3) v = (uni(rng) * 4.0 - 2.0) * (p.max - p.min) + p.min;  // out of range
            else v = p.min + uni(rng) * (p.max - p.min);
            e.setParam(p.id, v);
        }
        e.setParam(9999, 1.0);  // unknown id must be ignored
        if (rng() % 7 == 0) e.noteOn(int(rng() % 200) - 20);
        if (rng() % 7 == 0) e.noteOff(int(rng() % 128));
        Transport tr;
        tr.playing = rng() % 5 != 0;
        const double pick = uni(rng);
        tr.tempo = pick < 0.2 ? nasty[rng() % 8] : 20.0 + uni(rng) * 300.0;
        tr.beatPos = uni(rng) < 0.1 ? nasty[rng() % 8] : uni(rng) * 1000.0;
        const uint32_t n = 1 + rng() % 256;
        e.process(l.data(), r.data(), n, tr);
        for (uint32_t i = 0; i < n; ++i) {
            finite = finite && std::isfinite(l[i]) && std::isfinite(r[i]);
            peak = std::max(peak, double(std::fabs(l[i])));
        }
    }
    CHECK(finite, "non-finite sample produced");
    CHECK(peak <= 4.0, "output exceeds the safety clamp (%g)", peak);

    // Steady play with extreme-but-valid settings stays finite too.
    for (int corner = 0; corner < 64; ++corner) {
        KiksetEngine g;
        g.setSampleRate(fs);
        for (const auto& p : kParams) g.setParam(p.id, ((corner >> (p.id % 6)) & 1) ? p.max : p.min);
        Transport tr{true, 0.0, 145.0};
        bool ok = true;
        for (int blk = 0; blk < 200; ++blk) {
            g.process(l.data(), r.data(), 256, tr);
            tr.beatPos += 256 * 145.0 / 60.0 / fs;
            for (float v : l) ok = ok && std::isfinite(v);
        }
        CHECK(ok, "corner %d produced non-finite output", corner);
    }
    return finish("robustness_test");
}
