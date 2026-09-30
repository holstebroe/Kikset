#include "KickPhase.hpp"

#include <algorithm>
#include <cstring>

namespace kikset {

double KickPhaseModel::shapeS(double u) const {
    auto sh = [&](double k) {
        const double e = std::exp(-k);
        return (std::exp(-k * u) - e) / (1.0 - e);
    };
    return (1.0 - s_.p) * sh(s_.k) + s_.p * sh(8.0 * s_.k);
}

double KickPhaseModel::dC(double u) const {
    return s_.f0 * s_.S * s_.T16 * (std::exp2(s_.O * shapeS(u)) - 1.0);
}

void KickPhaseModel::build(const KickShape& s) {
    s_ = s;
    // Integrate dC/du over u = v^2, du = 2v dv, with Simpson per cell.
    C_[0] = 0.0;
    auto g = [&](double v) { return dC(v * v) * 2.0 * v; };  // dC/dv
    for (int i = 0; i <= N; ++i) D_[i] = g(double(i) / N);
    for (int i = 0; i < N; ++i) {
        const double v0 = double(i) / N, v1 = double(i + 1) / N;
        const double gm = g(0.5 * (v0 + v1));
        C_[i + 1] = C_[i] + (v1 - v0) / 6.0 * (D_[i] + 4.0 * gm + D_[i + 1]);
    }
}

double KickPhaseModel::phase(double t) const {
    if (t < 0.0) t = 0.0;
    const double span = s_.S * s_.T16;
    const double u = t / span;
    double c;
    if (u >= 1.0) {
        c = C_[N];
    } else {
        const double v = std::sqrt(u) * N;
        int i = std::min(int(v), N - 1);
        const double x = v - i, h = 1.0 / N;
        const double x2 = x * x, x3 = x2 * x;
        c = (2 * x3 - 3 * x2 + 1) * C_[i] + (x3 - 2 * x2 + x) * h * D_[i] +
            (-2 * x3 + 3 * x2) * C_[i + 1] + (x3 - x2) * h * D_[i + 1];
    }
    return s_.phi0 + s_.f0 * t + c;
}

double KickPhaseModel::freq(double t) const {
    if (t < 0.0) t = 0.0;
    const double u = t / (s_.S * s_.T16);
    return u >= 1.0 ? s_.f0 : s_.f0 * std::exp2(s_.O * shapeS(u));
}

bool SolverInput::operator==(const SolverInput& o) const {
    return std::memcmp(this, &o, sizeof(SolverInput)) == 0;
}

namespace {

constexpr int kVars = 4;  // tau, O, k, phi0

struct Vars {
    double x[kVars];
};

KickShape apply(const KickShape& b, const Vars& v) {
    KickShape s = b;
    s.tau = v.x[0];
    s.O = v.x[1];
    s.k = v.x[2];
    s.phi0 = v.x[3];
    return s;
}

double phaseAtAnchor(const SolverInput& in, const Vars& v, KickPhaseModel& m) {
    const KickShape s = apply(in.base, v);
    m.build(s);
    return m.phase(in.anchorTime - s.tau);
}

struct Run {
    Vars v;
    double cost;
    double resid;
    int iters;
    bool ok;
};

}  // namespace

SolverResult solveAnchor(const SolverInput& in) {
    KickPhaseModel m;
    const KickShape& b = in.base;
    const double st = std::clamp(in.lockStyle, 0.0, 1.0);

    // Bounds and normalisation scales (perceptual units).
    const double lo[kVars] = {0.0, std::max(0.0, b.O - 0.75), std::max(1.0, b.k * 0.6), b.phi0 - 0.5};
    const double hi[kVars] = {0.006, std::min(6.5, b.O + 0.75), b.k * 1.6, b.phi0 + 0.5};
    const double scale[kVars] = {0.003, 0.3, 0.3 * b.k, 0.25};
    const double w[kVars] = {1.0 + 49.0 * st, 20.0 - 19.0 * st, 20.0 - 19.0 * st, 4.0};
    // Metric in raw units: cost = sum w_i (dx_i / scale_i)^2 -> W_i = w_i / scale_i^2.
    double W[kVars];
    for (int i = 0; i < kVars; ++i) W[i] = w[i] / (scale[i] * scale[i]);

    Vars v0{{b.tau, b.O, b.k, b.phi0}};
    for (int i = 0; i < kVars; ++i) v0.x[i] = std::clamp(v0.x[i], lo[i], hi[i]);
    const double P0 = phaseAtAnchor(in, v0, m);
    const double base = std::round(P0 - in.targetPhase);

    Run best{v0, 1e300, 1.0, 0, false};
    bool haveBest = false;

    for (int cand = 0; cand < 5; ++cand) {
        const int offs[5] = {0, -1, 1, -2, 2};
        const double N = base + offs[cand];
        Vars v = v0;
        bool fixedVar[kVars] = {false, false, false, false};
        Run r{v, 0.0, 1.0, 0, false};
        for (int it = 0; it < 24; ++it) {
            const double P = phaseAtAnchor(in, v, m);
            const double res = P - in.targetPhase - N;
            r.resid = res;
            r.iters = it;
            if (std::fabs(res) < 1e-10) {
                r.ok = true;
                break;
            }
            double J[kVars];
            for (int i = 0; i < kVars; ++i) {
                const double h = scale[i] * 1e-3;
                Vars vp = v, vm = v;
                vp.x[i] += h;
                vm.x[i] -= h;
                J[i] = (phaseAtAnchor(in, vp, m) - phaseAtAnchor(in, vm, m)) / (2 * h);
            }
            double denom = 0.0;
            for (int i = 0; i < kVars; ++i)
                if (!fixedVar[i]) denom += J[i] * J[i] / W[i];
            if (denom < 1e-18) break;
            const double lambda = -res / denom;
            bool clamped = false;
            Vars nv = v;
            for (int i = 0; i < kVars; ++i) {
                if (fixedVar[i]) continue;
                double x = v.x[i] + lambda * J[i] / W[i];
                if (x < lo[i]) { x = lo[i]; fixedVar[i] = true; clamped = true; }
                if (x > hi[i]) { x = hi[i]; fixedVar[i] = true; clamped = true; }
                nv.x[i] = x;
            }
            v = nv;
            (void)clamped;
        }
        r.v = v;
        double c = 0.0;
        for (int i = 0; i < kVars; ++i) {
            const double d = (v.x[i] - v0.x[i]) / scale[i];
            c += w[i] * d * d;
        }
        r.cost = c;
        if (r.ok && (!haveBest || !best.ok || c < best.cost)) { best = r; haveBest = true; }
        else if (!haveBest && (r.cost < best.cost || best.cost == 1e300)) best = r;
        if (haveBest && best.ok && cand >= 2) break;  // three candidates are enough when one worked
    }

    SolverResult out;
    out.shape = apply(b, best.v);
    m.build(out.shape);
    out.residual = wrapHalf(m.phase(in.anchorTime - out.shape.tau) - in.targetPhase);
    out.converged = std::fabs(out.residual) < 1e-8;
    out.nudgeMs = (out.shape.tau - b.tau) * 1000.0;
    out.dPitchCents = 1200.0 * (out.shape.O - b.O);
    out.dPhaseDeg = (out.shape.phi0 - b.phi0) * 360.0;
    out.iterations = best.iters;
    return out;
}

const SolverResult& SolverCache::get(const SolverInput& in) {
    for (auto& e : e_)
        if (e.used && e.in == in) return e.out;
    Entry& e = e_[next_];
    next_ = (next_ + 1) % e_.size();
    e.in = in;
    e.out = solveAnchor(in);
    e.used = true;
    return e.out;
}

}  // namespace kikset
