// Per-beat plan: everything the voices need, computed analytically from
// (tempo, root, params) before the beat is rendered (design §2, §3, §4, §6).
#pragma once
#include "KickPhase.hpp"
#include "Params.hpp"
#include "Saturator.hpp"

namespace kikset {

struct KickAmpParams {
    double T16 = 0.1, attack = 0.0004, length = 1.05, shape = 1.5, fishtail = 0.2;
    double gateEnd = 1.0, gateFade = 0.002;  // seconds from kick trigger
};

// Amplitude envelope at t seconds after the kick trigger.
double kickAmp(const KickAmpParams& a, double t);

struct KickPlan {
    KickShape shape;
    KickPhaseModel model;
    KickAmpParams amp;
    SatType sat = SatType::Soft;
    double gain = 1.0;       // drive gain
    double bias = 0.0;       // pre-gain offset (already scaled)
    double envFollow = 0.3;
    double tone = 0.0;
    double fund = 1.0;       // fundamental gain at full amplitude (level compensation)
    double emphMag = 1.0;    // |pre-emphasis| at f0: the amplitude the shaper really sees
    int clickIndex = 0;
    double clickGain = 0.25;
    double beatStart = 0.0;  // kick trigger relative to beat start == shape.tau
};

struct NotePlan {
    bool active = false;
    int step = 0;              // 1..3
    double onset = 0.0;        // seconds from beat start (includes Push)
    double freq = 43.65;       // Hz
    double phase0 = 0.0;       // cycles at onset
    double gain = 1.0;         // Flow + emphasis
    double cutoff = 300.0;     // Hz, already accent/analog adjusted
    double envMod = 3.0;       // octaves, accent adjusted
    double fDecay = 0.03;      // seconds (time constant)
    double attack = 0.001, decay = 0.03, gate = 0.07;  // seconds
    double wave = 0.0, sub = 0.3, res = 0.05, drive = 0.2, thirdDb = -2.0;
    int filterType = 0;
};

struct BeatPlan {
    double tempo = 145.0, T16 = 0.1, Tbeat = 0.4, f0 = 43.65;
    int root = 5;
    bool followMode = false;
    KickPlan kick;
    NotePlan notes[3];
    int firstActive = -1;      // index 0..2, -1 if none
    SolverResult solve;
    double flowGainDb[3] = {0, 0, 0};
    double kickRms[4] = {0, 0, 0, 0};
};

double kickFundamental(int rootPitchClass, double tuneSemitones);

void buildBeatPlan(BeatPlan& out, const ParamSet& p, double tempo, int root, SolverCache& cache);

}  // namespace kikset
