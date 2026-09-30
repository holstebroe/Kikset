// Kick saturation stage (design §5.2.1): memoryless shapers, analytic bias
// compensation, matched emphasis pair and a 4x half-band oversampler whose
// group delay is known so the voice can compensate it by evaluating the
// analytic generator ahead in time.
#pragma once
#include <array>
#include <cmath>

namespace kikset {

enum class SatType : int { Soft = 0, Tube = 1, Diode = 2, Tape = 3 };

double shape(SatType t, double x);

// 31-tap half-band FIR, used as a 2x interpolator / decimator.
class HalfBand {
public:
    static constexpr int M = 15;
    static constexpr int TAPS = 2 * M + 1;
    HalfBand();
    void reset() { buf_.fill(0.0); pos_ = 0; }
    void push(double x) {
        pos_ = (pos_ + TAPS - 1) % TAPS;
        buf_[pos_] = x;
    }
    double out() const {
        double acc = 0.0;
        for (int i = 0; i < TAPS; ++i) acc += h_[i] * buf_[(pos_ + i) % TAPS];
        return acc;
    }

private:
    std::array<double, TAPS> h_{};
    std::array<double, TAPS> buf_{};
    int pos_ = 0;
};

// Base-rate in -> base-rate out, shaper called on the 4x samples.
class Oversampler4 {
public:
    // Group delay in base-rate samples (measured by the DSP test).
    static constexpr double kDelay = 21.75;
    void reset() { upA_.reset(); upB_.reset(); dnB_.reset(); dnA_.reset(); }
    template <class F>
    double process(double x, F&& shaper) {
        double a[2];
        upA_.push(x);
        a[0] = 2.0 * upA_.out();
        upA_.push(0.0);
        a[1] = 2.0 * upA_.out();
        double mid[2];
        for (int i = 0; i < 2; ++i) {
            double b[2];
            upB_.push(a[i]);
            b[0] = 2.0 * upB_.out();
            upB_.push(0.0);
            b[1] = 2.0 * upB_.out();
            dnB_.push(shaper(b[0]));
            dnB_.push(shaper(b[1]));
            mid[i] = dnB_.out();
        }
        dnA_.push(mid[0]);
        dnA_.push(mid[1]);
        return dnA_.out();
    }

private:
    HalfBand upA_, upB_, dnB_, dnA_;
};

// Matched pre/de-emphasis pair built on a TPT one-pole low-pass.
// pre:  y = (1+a)x - a*lp(x)        de: exact inverse
class EmphasisPair {
public:
    void reset() { sp_ = sd_ = 0.0; }
    void set(double amount, double fc, double fs) {
        a_ = amount;
        const double g = std::tan(3.14159265358979323846 * fc / fs);
        G_ = g / (1.0 + g);
    }
    double pre(double x) {
        const double v = (x - sp_) * G_;
        const double lp = v + sp_;
        sp_ = lp + v;
        return (1.0 + a_) * x - a_ * lp;
    }
    double de(double y) {
        const double b = 1.0 - G_;
        const double x = (y + a_ * b * sd_) / (1.0 + a_ * b);
        const double v = (x - sd_) * G_;
        const double lp = v + sd_;
        sd_ = lp + v;
        return x;
    }

private:
    double a_ = 0.0, G_ = 0.1, sp_ = 0.0, sd_ = 0.0;
};

// Mean and fundamental gain of shape(g*sin + bias) over one cycle (quadrature).
struct ShaperStats {
    double mean = 0.0;
    double fund = 1.0;
};
ShaperStats shaperStats(SatType t, double gain, double bias, int points = 64);

}  // namespace kikset
