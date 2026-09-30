#include "Voices.hpp"

#include <algorithm>

namespace kikset {

// ---------------------------------------------------------------- kick

void KickVoice::start(const KickPlan& plan, double onsetAbs, double fs, const ClickBank* clicks) {
    plan_ = plan;
    onset_ = onsetAbs;
    fs_ = fs;
    clicks_ = clicks;
    active_ = true;
    primed_ = false;
    killAt_ = -1.0;
    os_.reset();
    emph_.reset();
    const double a = plan_.tone > 0 ? plan_.tone * 2.0 : plan_.tone * 0.6;
    emph_.set(a, 300.0, fs * 4.0);
    tapeLp_ = 0.0;
    const double drive = (plan_.gain - 1.0) / 29.0;
    const double fc = 14000.0 - 6000.0 * drive;
    const double g = std::tan(kPi * std::min(fc, 0.45 * fs * 4.0) / (fs * 4.0));
    tapeG_ = g / (1.0 + g);
    dcTick_ = 0;
}

void KickVoice::kill(double abs) {
    if (active_ && killAt_ < 0.0) killAt_ = abs;
}

void KickVoice::cancelPending() {
    if (active_ && !primed_) active_ = false;
}

double KickVoice::generator(double t) const {
    if (t < 0.0) return 0.0;
    // sine body scaled by the drive-follow factor m (folded in *before* the
    // oversampler so it is time-aligned with the signal it scales)
    const double m = plan_.envFollow * kickAmp(plan_.amp, t) + (1.0 - plan_.envFollow);
    return m * std::sin(2.0 * kPi * frac(plan_.model.phase(t)));
}

double KickVoice::pipeline(double tIn) {
    // tIn = time of the generator sample fed now (already includes delay lead)
    const double x = generator(tIn);
    const double gm = plan_.gain;
    const SatType st = plan_.sat;
    const double off = plan_.gain * plan_.bias;
    const bool tape = st == SatType::Tape;
    return os_.process(x, [&](double b) {
        double y = shape(st, gm * emph_.pre(b) + off);
        if (tape) {
            const double v = (y - tapeLp_) * tapeG_;
            const double lp = v + tapeLp_;
            tapeLp_ = lp + v;
            y = lp;
        }
        return emph_.de(y);
    });
}

double KickVoice::render(double abs) {
    if (!active_) return 0.0;
    const double ts = abs - onset_;  // samples since trigger
    if (ts < 0.0) return 0.0;
    const double t = ts / fs_;
    const double lead = Oversampler4::kDelay / fs_;
    const auto& A = plan_.amp;

    if (!primed_) {
        // Feed the pre-roll so the delay-compensated pipeline is aligned at t = 0.
        const int pre = int(Oversampler4::kDelay) + 2;
        for (int j = -pre; j < 0; ++j) {
            const double tt = (ts + j) / fs_ + lead;
            pipeline(tt);
        }
        primed_ = true;
    }

    double y = pipeline(t + lead);

    const double a = kickAmp(A, t);
    const double m = plan_.envFollow * a + (1.0 - plan_.envFollow);
    // Analytic DC removal (no high-pass, so the fundamental phase is untouched).
    if ((dcTick_++ & 1) == 0) {
        dcHold_ = shaperStats(plan_.sat, plan_.gain * plan_.emphMag * m, plan_.gain * plan_.bias).mean;
    }
    y -= dcHold_;
    y /= plan_.fund;
    y = m > 1e-9 ? y * (a / m) : 0.0;

    if (clicks_ && plan_.clickGain > 0.0) y += plan_.clickGain * clicks_->at(plan_.clickIndex, ts);

    if (killAt_ >= 0.0) {
        const double f = 1.0 - (abs - killAt_) / (0.003 * fs_);
        if (f <= 0.0) { active_ = false; return 0.0; }
        y *= f;
    }
    if (t > A.gateEnd + 40.0 / fs_ && ts > (clicks_ ? clicks_->length(plan_.clickIndex) : 0)) active_ = false;
    return y;
}

// ---------------------------------------------------------------- bass

void BassVoice::schedule(const NotePlan& n, double onsetAbs) {
    const int next = (qTail_ + 1) % kQ;
    if (next == qHead_) return;  // full: drop
    q_[qTail_] = {n, onsetAbs};
    qTail_ = next;
}

void BassVoice::kill(double abs) {
    if (active_ && killAt_ < 0.0) killAt_ = abs;
}

void BassVoice::startNote(const NotePlan& n, double onsetAbs) {
    cur_ = n;
    onset_ = onsetAbs;
    active_ = true;
    killAt_ = -1.0;
    // Each note starts from a clean filter: the VCA is closed here, so this is
    // inaudible and makes every beat render identically.
    ladder_.reset();
    svf_.reset();
    eq_.reset();
    eq_.set(3.0 * n.freq, n.thirdDb, 2.0, fs_);
}

namespace {
inline double polyblep(double t, double dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}
// falling saw with PolyBLEP: 1 - 2*phi (+ correction at the wrap)
inline double sawFalling(double ph, double dt) { return 1.0 - 2.0 * ph + polyblep(ph, dt); }
}  // namespace

double BassVoice::render(double abs) {
    while (qHead_ != qTail_ && q_[qHead_].onset <= abs) {
        startNote(q_[qHead_].n, q_[qHead_].onset);
        qHead_ = (qHead_ + 1) % kQ;
    }
    if (!active_) return 0.0;
    const NotePlan& n = cur_;
    const double t = (abs - onset_) / fs_;
    if (t < 0.0) return 0.0;
    const double release = 0.002;
    if (t > n.gate + release + 0.008) { active_ = false; return 0.0; }  // EQ ring-out

    double amp = std::exp(-t / n.decay);
    if (t < n.attack) amp *= t / n.attack;
    if (t > n.gate) amp *= std::max(0.0, 1.0 - (t - n.gate) / release);

    // oscillator: phase is analytic, never accumulated
    const double ph = frac(n.phase0 + n.freq * t);
    const double dt = n.freq / fs_;
    const double saw = sawFalling(ph, dt);
    const double sq = saw - sawFalling(frac(ph + 0.5), dt);
    double osc = saw * (1.0 - n.wave) + sq * 0.7 * n.wave;

    const double fc = n.cutoff * std::exp2(n.envMod * std::exp(-t / n.fDecay));
    double y = n.filterType == 0 ? ladder_.process(osc * 0.5, fc, n.res, fs_)
                                 : svf_.process(osc * 0.5, fc, n.res, fs_);
    y += n.sub * 0.6 * std::sin(2.0 * kPi * ph);
    y *= amp;
    const double g = 1.0 + 5.0 * n.drive;
    y = std::tanh(g * y) / std::sqrt(g);
    y = eq_.process(y);
    y *= n.gain;

    if (killAt_ >= 0.0) {
        const double f = 1.0 - (abs - killAt_) / (0.003 * fs_);
        if (f <= 0.0) { active_ = false; return 0.0; }
        y *= f;
    }
    return y;
}

}  // namespace kikset
