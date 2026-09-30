#include "BeatPlan.hpp"

#include <algorithm>
#include <cstdint>

namespace kikset {

double kickAmp(const KickAmpParams& a, double t) {
    if (t < 0.0) return 0.0;
    double att = 1.0;
    if (t < a.attack) {
        const double x = t / a.attack;
        att = 0.5 - 0.5 * std::cos(kPi * x);
    }
    const double v = std::min(1.0, t / (a.length * a.T16));
    double body = std::pow(1.0 - v, a.shape);
    if (a.fishtail > 0.0) {
        const double c = 0.12 * a.T16, w = 0.06 * a.T16;
        const double x = (t - c) / w;
        body *= 1.0 - 0.65 * a.fishtail * std::exp(-x * x);
    }
    double g = 1.0;
    const double gs = a.gateEnd - a.gateFade;
    if (t >= a.gateEnd) g = 0.0;
    else if (t > gs) g = 0.5 + 0.5 * std::cos(kPi * (t - gs) / a.gateFade);
    return att * body * g;
}

double kickFundamental(int pc, double tune) {
    pc = ((pc % 12) + 12) % 12;
    double f = 55.0 * std::pow(2.0, (pc - 9) / 12.0);
    while (f < 38.0) f *= 2.0;
    while (f >= 76.0) f *= 0.5;
    return f * std::pow(2.0, tune / 12.0);
}

namespace {
// Deterministic per-step pseudo-random in [-1, 1].
double stepRand(int step, int salt) {
    uint32_t h = uint32_t(step) * 2654435761u + uint32_t(salt) * 40503u + 12345u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return double(h & 0xFFFFu) / 32767.5 - 1.0;
}
}  // namespace

void buildBeatPlan(BeatPlan& b, const ParamSet& p, double tempo, int root, SolverCache& cache) {
    b.tempo = tempo;
    b.T16 = 60.0 / (tempo * 4.0);
    b.Tbeat = 4.0 * b.T16;
    b.root = root;
    b.f0 = kickFundamental(root, p[P_KickTune]);
    b.followMode = p[P_PhaseMode] > 0.5;
    const double psi = p[P_TargetPhase] / 360.0;
    const double pushS = p[P_Push] * 1e-3;

    // --- active steps, first anchor
    bool active[3];
    b.firstActive = -1;
    for (int i = 0; i < 3; ++i) {
        active[i] = p.stepOn(i + 1);
        if (active[i] && b.firstActive < 0) b.firstActive = i;
    }

    // --- kick shape + solver
    KickShape s;
    s.f0 = b.f0;
    s.O = p[P_Sweep];
    s.k = p[P_Curve];
    s.p = p[P_Punch];
    s.S = p[P_Settle];
    s.T16 = b.T16;
    s.phi0 = p[P_StartPhase] / 360.0;
    s.tau = 0.0;
    double tA = 0.0;
    if (b.firstActive >= 0) {
        tA = (b.firstActive + 1) * b.T16 + pushS;
        SolverInput in;
        in.base = s;
        in.anchorTime = tA;
        in.targetPhase = psi;
        in.lockStyle = p[P_LockStyle];
        b.solve = cache.get(in);
        s = b.solve.shape;
    } else {
        b.solve = SolverResult{};
        b.solve.shape = s;
    }
    KickPlan& k = b.kick;
    k.shape = s;
    k.model.build(s);
    k.beatStart = s.tau;

    // --- kick amplitude + tail gate (§3.3)
    KickAmpParams& a = k.amp;
    a.T16 = b.T16;
    a.length = p[P_Length];
    a.shape = p[P_Shape];
    a.fishtail = p[P_Fishtail];
    a.attack = 0.0004;
    a.gateFade = 0.002;
    double endBeat = b.Tbeat - 0.001;  // before the next kick
    double endRel;
    if (b.firstActive >= 0 && !b.followMode) {
        endBeat = std::min(endBeat, tA + p[P_Gel] * b.T16);
        for (int i = b.firstActive + 1; i < 3; ++i)
            if (active[i]) {
                endBeat = std::min(endBeat, (i + 1) * b.T16 + pushS - 0.001);
                break;
            }
    }
    endRel = std::max(endBeat - s.tau, a.gateFade + 0.0005);
    a.gateEnd = endRel;

    // --- kick voice / saturation params
    const double drive = p[P_KickDrive];
    k.sat = SatType(int(p[P_SatType]));
    k.gain = 1.0 + 29.0 * drive;
    k.bias = 0.3 * p[P_SatBias];
    k.envFollow = p[P_SatEnvFollow];
    k.tone = p[P_SatTone];
    {   // magnitude of the pre-emphasis at f0 (analytic, TPT one-pole corner 300 Hz at 4x rate)
        const double a = k.tone > 0 ? k.tone * 2.0 : k.tone * 0.6;
        const double fs4 = 4.0 * 48000.0;  // nominal; the corner is far above f0 so this is insensitive
        const double g = std::tan(kPi * 300.0 / fs4);
        const double w = std::tan(kPi * b.f0 / fs4) / g;  // normalised frequency
        const double lpRe = 1.0 / (1.0 + w * w), lpIm = -w / (1.0 + w * w);
        k.emphMag = std::hypot(1.0 + a - a * lpRe, -a * lpIm);
    }
    k.fund = shaperStats(k.sat, k.gain * k.emphMag, k.gain * k.bias).fund;
    k.clickIndex = int(p[P_ClickType]);
    k.clickGain = p[P_ClickLevel] <= -59.9 ? 0.0 : std::pow(10.0, p[P_ClickLevel] / 20.0);

    // --- kick RMS per step window (for Flow)
    for (int n = 0; n < 4; ++n) {
        double acc = 0.0;
        constexpr int M = 96;
        for (int i = 0; i < M; ++i) {
            const double t = (n + (i + 0.5) / M) * b.T16 - s.tau;
            const double v = kickAmp(a, t);
            acc += v * v;
        }
        b.kickRms[n] = std::sqrt(acc / M);
    }

    // --- bass notes
    const double flow = p[P_Flow], roll = p[P_Roll];
    const int oct = int(p[P_BassOct]);
    const int interval = int(p[P_Interval]);
    const double ratio = std::pow(2.0, oct + interval / 12.0);
    const double fb = b.f0 * ratio;
    constexpr double fa = 0.35, fbb = 0.65, kappa = 2.0;
    for (int i = 0; i < 3; ++i) {
        NotePlan& n = b.notes[i];
        n = NotePlan{};
        n.active = active[i];
        n.step = i + 1;
        if (!n.active) continue;
        const bool first = (i == b.firstActive);
        n.onset = (i + 1) * b.T16 + pushS;
        n.freq = fb;
        if (b.followMode && b.firstActive >= 0) {
            const double pa = k.model.phase(tA - s.tau);
            const double pn = k.model.phase(n.onset - s.tau);
            n.phase0 = frac(psi + ratio * (pn - pa));
        } else {
            n.phase0 = frac(psi);
        }
        // Flow (§6)
        const double K0 = std::max(b.kickRms[0], 1e-9);
        const double Kn = b.kickRms[i + 1];
        double g = 1.0 - flow * (fa * (first ? 1.0 : 0.0) + fbb * std::min(1.0, Kn / K0 * kappa));
        g = std::clamp(g, 0.2, 1.0);
        const double E = p.emph(i + 1) + roll * (i + 1 - 2);
        g *= std::pow(2.0, E);
        n.gain = g;
        b.flowGainDb[i] = 20.0 * std::log10(g);
        // Analog variation: seeded per step, never touches pitch or phase
        double analog = p[P_Analog];
        if (first && analog < 0.5) analog = 0.0;
        const double cents = 40.0 * analog * stepRand(i + 1, 1);
        const double tmul = 1.0 + 0.06 * analog * stepRand(i + 1, 2);
        n.cutoff = p[P_Cutoff] * std::pow(2.0, (cents / 1200.0) + 0.3 * E);
        n.envMod = p[P_EnvMod] * (1.0 + 0.5 * E);
        n.fDecay = std::max(1e-4, p[P_FDecay] * b.T16 * 0.5 * tmul);
        n.attack = std::max(5e-5, p[P_Attack] * 1e-3);
        n.decay = std::max(1e-4, p[P_Snap] * b.T16 * 0.5 * tmul);
        n.gate = p[P_Gate] * b.T16;
        n.wave = p[P_Wave];
        n.sub = p[P_Sub];
        n.res = p[P_Res];
        n.drive = p[P_BassDrive];
        n.thirdDb = p[P_Third];
        n.filterType = int(p[P_FilterType]);
    }
}

}  // namespace kikset
