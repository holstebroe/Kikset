// Zero-delay-feedback (TPT) filters for the bass voice (design §5.3).
#pragma once
#include <algorithm>
#include <cmath>

namespace kikset {

inline double fastTanh(double x) { return std::tanh(x); }

// 4-pole Moog-style ladder, tanh input stage, passband-gain compensated.
class Ladder24 {
public:
    void reset() { s_[0] = s_[1] = s_[2] = s_[3] = 0.0; }
    double process(double x, double fc, double res, double fs) {
        fc = std::clamp(fc, 10.0, 0.45 * fs);
        const double g = std::tan(3.14159265358979323846 * fc / fs);
        const double G = g / (1.0 + g);
        const double k = 3.95 * std::clamp(res, 0.0, 1.0);
        const double G2 = G * G, G4 = G2 * G2;
        const double beta = (1.0 - G);
        const double sigma = G2 * G * beta * s_[0] + G2 * beta * s_[1] + G * beta * s_[2] + beta * s_[3];
        double u = (x - k * sigma) / (1.0 + k * G4);
        u = fastTanh(1.5 * u) / 1.5;
        double in = u;
        for (int i = 0; i < 4; ++i) {
            const double v = (in - s_[i]) * G;
            const double y = v + s_[i];
            s_[i] = y + v;
            in = y;
        }
        return in * (1.0 + 0.6 * k);
    }

private:
    double s_[4] = {0, 0, 0, 0};
};

// 2-pole TPT state-variable low-pass (Juno/Virus-ish "OTA 12").
class SvfLp12 {
public:
    void reset() { s1_ = s2_ = 0.0; }
    double process(double x, double fc, double res, double fs) {
        fc = std::clamp(fc, 10.0, 0.45 * fs);
        const double g = std::tan(3.14159265358979323846 * fc / fs);
        const double k = 1.414 * (1.0 - std::clamp(res, 0.0, 1.0)) + 0.1 * res;
        const double hp = (x - (k + g) * s1_ - s2_) / (1.0 + g * (g + k));
        const double bp = g * hp + s1_;
        s1_ = g * hp + bp;
        const double lp = g * bp + s2_;
        s2_ = g * bp + lp;
        return lp;
    }

private:
    double s1_ = 0.0, s2_ = 0.0;
};

// RBJ peaking biquad (TDF-II) used for the 3rd-harmonic dip.
class Peaking {
public:
    void reset() { z1_ = z2_ = 0.0; }
    void set(double f, double gainDb, double q, double fs) {
        f = std::clamp(f, 10.0, 0.45 * fs);
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * 3.14159265358979323846 * f / fs;
        const double al = std::sin(w0) / (2.0 * q), c = std::cos(w0);
        const double a0 = 1.0 + al / A;
        b0_ = (1.0 + al * A) / a0;
        b1_ = -2.0 * c / a0;
        b2_ = (1.0 - al * A) / a0;
        a1_ = -2.0 * c / a0;
        a2_ = (1.0 - al / A) / a0;
    }
    double process(double x) {
        const double y = b0_ * x + z1_;
        z1_ = b1_ * x - a1_ * y + z2_;
        z2_ = b2_ * x - a2_ * y;
        return y;
    }

private:
    double b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0, z1_ = 0, z2_ = 0;
};

}  // namespace kikset
