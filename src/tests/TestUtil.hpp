#pragma once
#include <cmath>
#include <cstdio>
#include <vector>

#include "../core/KiksetEngine.hpp"

inline int g_failures = 0;
inline int g_checks = 0;
#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        ++g_checks;                                                        \
        if (!(cond)) {                                                     \
            ++g_failures;                                                  \
            std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond);    \
            std::printf(__VA_ARGS__);                                      \
            std::printf("\n");                                             \
        }                                                                  \
    } while (0)

inline int finish(const char* name) {
    std::printf("%s: %d checks, %d failures\n", name, g_checks, g_failures);
    return g_failures ? 1 : 0;
}

// Least-squares fit of A*sin(2*pi*f*(t-t0) + phi) over [t0-w/2, t0+w/2]; returns phi in cycles.
inline double fitPhase(const std::vector<float>& x, double fs, double f, double t0, double w, double* amp = nullptr) {
    // Least squares over (1, t) * (DC, sin, cos): the linear-in-t terms absorb the
    // amplitude slope of a decaying kick, so the phase at t0 is unbiased.
    // Use w = an integer number of periods so harmonics stay orthogonal.
    constexpr int K = 6;
    double m[K][K + 1] = {};
    const long i0 = long((t0 - w / 2) * fs), i1 = long((t0 + w / 2) * fs);
    for (long i = i0; i <= i1; ++i) {
        if (i < 0 || i >= long(x.size())) continue;
        const double t = i / fs - t0;
        const double th = 2.0 * kikset::kPi * f * t;
        const double s = std::sin(th), c = std::cos(th), tn = t / (w / 2);
        const double b[K] = {1.0, s, c, tn * s, tn * c, tn};
        for (int r = 0; r < K; ++r) {
            for (int q = 0; q < K; ++q) m[r][q] += b[r] * b[q];
            m[r][K] += b[r] * x[i];
        }
    }
    for (int c = 0; c < K; ++c) {  // Gauss-Jordan
        for (int r = 0; r < K; ++r) {
            if (r == c) continue;
            const double k = m[r][c] / m[c][c];
            for (int j = c; j <= K; ++j) m[r][j] -= k * m[c][j];
        }
    }
    const double cS = m[1][K] / m[1][1], cC = m[2][K] / m[2][2];
    if (amp) *amp = std::hypot(cS, cC);
    return std::atan2(cC, cS) / (2.0 * kikset::kPi);
}
