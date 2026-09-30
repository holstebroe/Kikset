#include "ClickBank.hpp"

#include <algorithm>
#include <cmath>

namespace kikset {

namespace {
constexpr double kPi = 3.14159265358979323846;
double sinc(double x) { return std::fabs(x) < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x); }
}  // namespace

void ClickBank::init(double fs) {
    clicks_.clear();
    const double ratio = 48000.0 / fs;  // source samples per output sample
    const int half = 16;                // windowed-sinc half width (source samples)
    const double cutoff = std::min(1.0, fs / 48000.0);
    // 1-pole high-pass at ~1 kHz keeps the click out of the phase-critical band.
    const double a = std::exp(-2.0 * kPi * 1000.0 / fs);
    for (int c = 0; c < kClickCount; ++c) {
        const ClickData& d = kClicks[c];
        const int outLen = int(std::ceil(d.length / ratio));
        std::vector<float> out(outLen + 2, 0.0f);
        for (int i = 0; i < outLen; ++i) {
            const double pos = i * ratio;
            const int c0 = int(std::floor(pos));
            double acc = 0.0;
            for (int j = c0 - half + 1; j <= c0 + half; ++j) {
                if (j < 0 || j >= d.length) continue;
                const double x = pos - j;
                const double w = 0.5 + 0.5 * std::cos(kPi * x / half);
                acc += d.samples[j] / 32768.0 * cutoff * sinc(x * cutoff) * w;
            }
            out[i] = float(acc);
        }
        double xp = 0.0, yp = 0.0;
        for (int i = 0; i < outLen; ++i) {
            const double x = out[i];
            const double y = 0.5 * (1.0 + a) * (x - xp) + a * yp;
            xp = x;
            yp = y;
            out[i] = float(y);
        }
        clicks_.push_back(std::move(out));
    }
}

const char* ClickBank::name(int i) const {
    return (i >= 0 && i < kClickCount) ? kClicks[i].name : "";
}

int ClickBank::length(int index) const {
    if (clicks_.empty()) return 0;
    index = std::clamp(index, 0, count() - 1);
    return int(clicks_[index].size()) - 2;
}

float ClickBank::at(int index, double pos) const {
    if (clicks_.empty() || pos < 0.0) return 0.0f;
    index = std::clamp(index, 0, count() - 1);
    const auto& v = clicks_[index];
    const int i = int(pos);
    if (i >= int(v.size()) - 2) return 0.0f;
    const float f = float(pos - i);
    return v[i] * (1.0f - f) + v[i + 1] * f;
}

}  // namespace kikset
