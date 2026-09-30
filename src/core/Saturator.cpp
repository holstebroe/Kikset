#include "Saturator.hpp"

#include <algorithm>

namespace kikset {

double shape(SatType t, double x) {
    switch (t) {
        case SatType::Soft: return std::tanh(x);
        case SatType::Tube: {
            constexpr double a = 0.4;
            return x >= 0.0 ? std::tanh(x) : std::tanh(x * (1.0 - a)) / (1.0 - a);
        }
        case SatType::Diode: {
            constexpr double n = 3.0;
            return x / std::pow(1.0 + std::pow(std::fabs(x), n), 1.0 / n);
        }
        case SatType::Tape: return x / (1.0 + std::fabs(x));
    }
    return x;
}

HalfBand::HalfBand() {
    // Blackman-windowed sinc, cutoff fs/4; even taps (except the centre) are zero.
    for (int i = 0; i < TAPS; ++i) {
        const int n = i - M;
        double s;
        if (n == 0) s = 0.5;
        else if (n % 2 == 0) s = 0.0;
        else s = std::sin(3.14159265358979323846 * n / 2.0) / (3.14159265358979323846 * n);
        const double w = 0.42 + 0.5 * std::cos(3.14159265358979323846 * n / (M + 1)) +
                         0.08 * std::cos(2.0 * 3.14159265358979323846 * n / (M + 1));
        h_[i] = s * w;
    }
}

ShaperStats shaperStats(SatType t, double gain, double bias, int points) {
    ShaperStats st;
    double mean = 0.0, f = 0.0;
    for (int i = 0; i < points; ++i) {
        const double th = 2.0 * 3.14159265358979323846 * (i + 0.5) / points;
        const double y = shape(t, gain * std::sin(th) + bias);
        mean += y;
        f += y * std::sin(th);
    }
    st.mean = mean / points;
    st.fund = 2.0 * f / points;
    if (std::fabs(st.fund) < 1e-6) st.fund = 1e-6;
    return st;
}

}  // namespace kikset
