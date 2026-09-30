#include "KiksetEngine.hpp"

#include <algorithm>
#include <cmath>

namespace kikset {

namespace {
constexpr double kLookahead = 0.012;  // seconds: lets negative Push schedule in time
}

KiksetEngine::KiksetEngine() { bass_.init(fs_); }

void KiksetEngine::setSampleRate(double fs) {
    fs_ = std::clamp(fs, 8000.0, 384000.0);
    clicks_.init(fs_);
    bass_.init(fs_);
    reset();
}

void KiksetEngine::reset() {
    kicks_[0] = KickVoice{};
    kicks_[1] = KickVoice{};
    bass_ = BassVoice{};
    bass_.init(fs_);
    wasRunning_ = false;
    nextBeat_ = 0;
    internalBeat_ = 0.0;
}

void KiksetEngine::noteOn(int key) {
    if (key < 0 || key > 127) return;
    if (params_[P_PlayMode] > 0.5 && nHeld_ == 0) {
        internalBeat_ = 0.0;
        wasRunning_ = false;  // forces a resync and a fresh start
    }
    for (int i = 0; i < nHeld_; ++i)
        if (held_[i] == key) {
            std::swap(held_[i], held_[nHeld_ - 1]);
            return;
        }
    if (nHeld_ < 16) held_[nHeld_++] = key;
}

void KiksetEngine::noteOff(int key) {
    for (int i = 0; i < nHeld_; ++i)
        if (held_[i] == key) {
            // keep order (last held = most recent)
            for (int j = i; j + 1 < nHeld_; ++j) held_[j] = held_[j + 1];
            --nHeld_;
            return;
        }
}

void KiksetEngine::allNotesOff() { nHeld_ = 0; }

int KiksetEngine::currentRoot() const {
    if (nHeld_ > 0) return held_[nHeld_ - 1] % 12;
    return int(params_[P_Key]);
}

void KiksetEngine::killVoices(double abs) {
    for (auto& k : kicks_) { k.cancelPending(); k.kill(abs); }
    bass_.cancelPending();
    bass_.kill(abs);
}

void KiksetEngine::scheduleBeat(long /*beatIdx*/, double beatStartAbs, double tempo) {
    buildBeatPlan(plan_, params_, tempo, currentRoot(), cache_);
    KickVoice& kv = kicks_[kickParity_];
    kickParity_ ^= 1;
    kv.start(plan_.kick, beatStartAbs + plan_.kick.beatStart * fs_, fs_, &clicks_);
    for (int i = 0; i < 3; ++i)
        if (plan_.notes[i].active) bass_.schedule(plan_.notes[i], beatStartAbs + plan_.notes[i].onset * fs_);
}

void KiksetEngine::process(float* L, float* R, uint32_t n, const Transport& trIn, float* kickStem,
                           float* bassStem) {
    const double tempo = std::isfinite(trIn.tempo) ? std::clamp(trIn.tempo, 20.0, 400.0) : 145.0;
    const bool gateMode = params_[P_PlayMode] > 0.5;
    bool running;
    double beat0;
    if (gateMode) {
        running = nHeld_ > 0;
        if (running && trIn.playing) beat0 = trIn.beatPos;
        else beat0 = internalBeat_;
    } else {
        running = trIn.playing;
        beat0 = trIn.beatPos;
    }
    beat0 = std::isfinite(beat0) ? std::clamp(beat0, -1.0e6, 1.0e7) : 0.0;
    const double dBeat = tempo / 60.0 / fs_;

    if (running) {
        const bool jumped = std::fabs(beat0 - expectBeat_) > 1.5 * dBeat;
        if (!wasRunning_ || jumped) {
            // Resync: kick waits for the next beat, bass for its next step.
            if (wasRunning_) killVoices(abs_);
            nextBeat_ = long(std::ceil(beat0 - 1e-7));
        }
    } else if (wasRunning_) {
        killVoices(abs_);
    }
    wasRunning_ = running;

    const double volDb = params_[P_Volume];
    const double vol = volDb <= -59.9 ? 0.0 : std::pow(10.0, volDb / 20.0);
    const double th = 0.5 * (params_[P_Balance] + 1.0) * 0.5 * kPi;
    const double kg = std::cos(th) * 1.41421356, bg = std::sin(th) * 1.41421356 * 1.6;  // bass is ~4 dB down at equal gain
    const double master = 0.5 * vol;

    for (uint32_t i = 0; i < n; ++i) {
        const double b = beat0 + i * dBeat;
        if (running) {
            for (int guard = 0; guard < 4; ++guard) {
                const double tTo = (double(nextBeat_) - b) / dBeat;
                if (tTo > kLookahead * fs_) break;
                if (tTo < -1.0) {  // beat already passed (jump, tempo change): wait for the next one
                    nextBeat_ = long(std::ceil(b - 1e-7));
                    continue;
                }
                scheduleBeat(nextBeat_, abs_ + tTo, tempo);
                ++nextBeat_;
            }
        }
        const double k = kicks_[0].render(abs_) + kicks_[1].render(abs_);
        const double s = bass_.render(abs_);
        double o = master * (kg * k + bg * s);
        if (!std::isfinite(o)) o = 0.0;
        o = std::clamp(o, -4.0, 4.0);
        L[i] = float(o);
        R[i] = float(o);
        if (kickStem) kickStem[i] = float(master * kg * k);
        if (bassStem) bassStem[i] = float(master * bg * s);
        abs_ += 1.0;
    }
    expectBeat_ = beat0 + n * dBeat;
    if (gateMode && !trIn.playing) internalBeat_ = beat0 + n * dBeat;
    beatPosition.store(expectBeat_, std::memory_order_relaxed);
}

BeatPreview renderBeatPreview(const ParamSet& p, double tempo, int root, double fs) {
    KiksetEngine e;
    e.setSampleRate(fs);
    e.params() = p;
    e.params().set(P_PlayMode, 0);
    e.params().set(P_Key, root);
    BeatPreview out;
    const size_t n = size_t(std::llround(fs * 60.0 / tempo));
    out.kick.assign(n, 0.f);
    out.bass.assign(n, 0.f);
    out.mix.assign(n, 0.f);
    std::vector<float> r(n);
    Transport tr{true, 0.0, tempo};
    e.process(out.mix.data(), r.data(), uint32_t(n), tr, out.kick.data(), out.bass.data());
    out.plan = e.lastPlan();
    return out;
}

}  // namespace kikset
