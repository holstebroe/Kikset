// Analytic kick phase model (design §3.1) and the anchor solver (§3.2).
#pragma once
#include <array>
#include <cmath>
#include <vector>

namespace kikset {

constexpr double kPi = 3.14159265358979323846;

inline double wrapHalf(double x) {  // -> (-0.5, 0.5]
    x -= std::floor(x + 0.5);
    return x <= -0.5 ? x + 1.0 : x;
}
inline double frac(double x) { return x - std::floor(x); }

struct KickShape {
    double f0 = 43.65;    // Hz, settled fundamental
    double O = 3.0;       // sweep depth, octaves
    double k = 12.0;      // curve
    double p = 0.3;       // punch
    double S = 0.8;       // settle, fraction of T16
    double T16 = 0.1034;  // seconds
    double phi0 = 0.0;    // start phase, cycles
    double tau = 0.0;     // trigger nudge, seconds (>= 0)
};

// Phase (in cycles) of the kick oscillator as a closed-form function of time.
// Built from a cumulative Simpson table in v = sqrt(u) (finer near the click),
// evaluated with cubic Hermite interpolation: no per-sample accumulation.
class KickPhaseModel {
public:
    static constexpr int N = 512;

    void build(const KickShape& s);
    const KickShape& shape() const { return s_; }
    // tLocal = seconds since the kick trigger (>= 0)
    double phase(double tLocal) const;
    double freq(double tLocal) const;
    double sweepCycles() const { return C_[N]; }
    double settleTime() const { return s_.S * s_.T16; }

private:
    double shapeS(double u) const;  // s(u): 1 -> 0
    double dC(double u) const;      // dC/du
    KickShape s_;
    std::array<double, N + 1> C_{};
    std::array<double, N + 1> D_{};  // dC/dv at nodes
};

struct SolverInput {
    KickShape base;
    double anchorTime = 0.0;   // seconds from beat start (first active bass onset)
    double targetPhase = 0.0;  // cycles
    double lockStyle = 0.4;    // 0 = timing (keep sweep), 1 = tone (keep timing)
    bool operator==(const SolverInput& o) const;
};

struct SolverResult {
    KickShape shape;
    double residual = 0.0;  // cycles, wrapped
    bool converged = true;
    double nudgeMs = 0.0;
    double dPitchCents = 0.0;  // change of start pitch
    double dPhaseDeg = 0.0;
    int iterations = 0;
};

SolverResult solveAnchor(const SolverInput& in);

// Small cache keyed on the full input.
class SolverCache {
public:
    const SolverResult& get(const SolverInput& in);

private:
    struct Entry {
        SolverInput in;
        SolverResult out;
        bool used = false;
    };
    std::array<Entry, 8> e_{};
    size_t next_ = 0;
};

}  // namespace kikset
