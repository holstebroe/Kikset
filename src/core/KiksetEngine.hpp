// The engine: owns voices, schedules beats from the host transport and renders.
// No CLAP/GUI includes; pure DSP, unit-testable (design §11).
#pragma once
#include <atomic>
#include <vector>

#include "BeatPlan.hpp"
#include "ClickBank.hpp"
#include "Params.hpp"
#include "Voices.hpp"

namespace kikset {

struct Transport {
    bool playing = false;
    double beatPos = 0.0;  // quarter notes at the start of the block
    double tempo = 145.0;
};

class KiksetEngine {
public:
    KiksetEngine();
    void setSampleRate(double fs);  // not real-time safe (resamples clicks)
    void reset();

    void setParam(uint32_t id, double v) { params_.set(id, v); }
    const ParamSet& params() const { return params_; }
    ParamSet& params() { return params_; }

    void noteOn(int key);
    void noteOff(int key);
    void allNotesOff();

    // Renders n frames. kickStem / bassStem (optional) receive the un-mixed voices.
    void process(float* left, float* right, uint32_t n, const Transport& tr,
                 float* kickStem = nullptr, float* bassStem = nullptr);

    // Diagnostics for the GUI / tests (audio thread writes, others read).
    const BeatPlan& lastPlan() const { return plan_; }
    std::atomic<double> beatPosition{0.0};
    const ClickBank& clicks() const { return clicks_; }

private:
    int currentRoot() const;
    void scheduleBeat(long beatIdx, double beatStartAbs, double tempo);
    void killVoices(double abs);

    ParamSet params_;
    double fs_ = 48000.0;
    ClickBank clicks_;
    SolverCache cache_;
    BeatPlan plan_;
    KickVoice kicks_[2];
    BassVoice bass_;

    double abs_ = 0.0;        // absolute sample clock
    long nextBeat_ = 0;
    bool wasRunning_ = false;
    double expectBeat_ = 0.0;
    double internalBeat_ = 0.0;
    int kickParity_ = 0;

    int held_[16] = {};
    int nHeld_ = 0;
};

// Pure helper: render exactly one beat of the current settings from a fresh
// engine, with separate kick and bass stems (used by the GUI scope and tests).
struct BeatPreview {
    std::vector<float> kick, bass, mix;
    BeatPlan plan;
};
BeatPreview renderBeatPreview(const ParamSet& p, double tempo, int root, double fs);

}  // namespace kikset
