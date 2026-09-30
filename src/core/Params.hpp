// Single source of truth for parameters: id, name, module path, range, default.
// CLAP glue, state and tools all read from this table.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace kikset {

enum ParamId : uint32_t {
    // Global
    P_Key = 0, P_PlayMode = 1, P_PhaseMode = 2, P_TargetPhase = 3, P_LockStyle = 4,
    P_Flow = 5, P_Roll = 6, P_Push = 7, P_Gel = 8, P_Balance = 9, P_Volume = 10,
    // Pattern
    P_Step1On = 20, P_Step2On = 21, P_Step3On = 22,
    P_Emph1 = 23, P_Emph2 = 24, P_Emph3 = 25,
    // Kick
    P_KickTune = 30, P_Sweep = 31, P_Curve = 32, P_Punch = 33, P_Settle = 34,
    P_StartPhase = 35, P_Length = 36, P_Shape = 37, P_Fishtail = 38, P_KickDrive = 39,
    P_ClickType = 40, P_ClickLevel = 41, P_SatType = 42, P_SatBias = 43,
    P_SatEnvFollow = 44, P_SatTone = 45,
    // Bass
    P_BassOct = 50, P_Interval = 51, P_Wave = 52, P_Sub = 53, P_FilterType = 54,
    P_Cutoff = 55, P_Res = 56, P_EnvMod = 57, P_FDecay = 58, P_Attack = 59,
    P_Snap = 60, P_Gate = 61, P_BassDrive = 62, P_Third = 63, P_Analog = 64,
};

constexpr uint32_t kMaxParamId = 80;

enum class Unit : uint8_t { None, Percent, Ms, Degrees, Db, Hz, Octaves, Semitones, T16 };

struct ParamInfo {
    uint32_t id;
    const char* name;
    const char* module;
    double min, max, def;
    bool stepped;
    bool logScale;
    Unit unit;
};

// clang-format off
inline constexpr std::array<ParamInfo, 48> kParams = {{
    {P_Key,         "Key",          "Global",  0, 11, 5, true,  false, Unit::None},
    {P_PlayMode,    "Play Mode",    "Global",  0, 1,  0, true,  false, Unit::None},
    {P_PhaseMode,   "Phase Mode",   "Global",  0, 1,  0, true,  false, Unit::None},
    {P_TargetPhase, "Target Phase", "Global",  0, 360, 0, false, false, Unit::Degrees},
    {P_LockStyle,   "Lock Style",   "Global",  0, 1,  0.4, false, false, Unit::None},
    {P_Flow,        "Flow",         "Feel",    0, 1,  0.5, false, false, Unit::None},
    {P_Roll,        "Roll",         "Feel",   -1, 1,  0, false, false, Unit::None},
    {P_Push,        "Push",         "Feel",  -10, 10, 0, false, false, Unit::Ms},
    {P_Gel,         "Gel",          "Feel",    0, 0.5, 0.35, false, false, Unit::T16},
    {P_Balance,     "Balance",      "Global", -1, 1, -0.1, false, false, Unit::None},
    {P_Volume,      "Volume",       "Global", -60, 6, 0, false, false, Unit::Db},

    {P_Step1On,     "Step 1 On",    "Pattern", 0, 1, 1, true, false, Unit::None},
    {P_Step2On,     "Step 2 On",    "Pattern", 0, 1, 1, true, false, Unit::None},
    {P_Step3On,     "Step 3 On",    "Pattern", 0, 1, 1, true, false, Unit::None},
    {P_Emph1,       "Emphasis 1",   "Pattern",-1, 1, 0, false, false, Unit::None},
    {P_Emph2,       "Emphasis 2",   "Pattern",-1, 1, 0, false, false, Unit::None},
    {P_Emph3,       "Emphasis 3",   "Pattern",-1, 1, 0, false, false, Unit::None},

    {P_KickTune,    "Kick Tune",    "Kick",  -12, 12, 0, false, false, Unit::Semitones},
    {P_Sweep,       "Sweep",        "Kick",    0, 6,  4.5, false, false, Unit::Octaves},
    {P_Curve,       "Curve",        "Kick",    1, 30, 14, false, false, Unit::None},
    {P_Punch,       "Punch",        "Kick",    0, 1,  0.35, false, false, Unit::None},
    {P_Settle,      "Settle",       "Kick",  0.3, 1,  0.7, false, false, Unit::T16},
    {P_StartPhase,  "Start Phase",  "Kick",    0, 180, 0, false, false, Unit::Degrees},
    {P_Length,      "Length",       "Kick",  0.5, 2.5, 1.5, false, false, Unit::T16},
    {P_Shape,       "Shape",        "Kick",  0.3, 4,  1.5, false, false, Unit::None},
    {P_Fishtail,    "Fishtail",     "Kick",    0, 1,  0.2, false, false, Unit::None},
    {P_KickDrive,   "Kick Drive",   "Kick/Drive", 0, 1, 0.2, false, false, Unit::None},
    {P_ClickType,   "Click Type",   "Kick/Click", 0, 7, 0, true, false, Unit::None},
    {P_ClickLevel,  "Click Level",  "Kick/Click",-60, 0, -18, false, false, Unit::Db},
    {P_SatType,     "Sat Type",     "Kick/Drive", 0, 3, 0, true, false, Unit::None},
    {P_SatBias,     "Sat Bias",     "Kick/Drive",-1, 1, 0, false, false, Unit::None},
    {P_SatEnvFollow,"Sat Env Follow","Kick/Drive", 0, 1, 0.3, false, false, Unit::None},
    {P_SatTone,     "Sat Tone",     "Kick/Drive",-1, 1, 0, false, false, Unit::None},

    {P_BassOct,     "Bass Octave",  "Bass",    0, 1,  0, true, false, Unit::None},
    {P_Interval,    "Interval",     "Bass",    0, 7,  0, true, false, Unit::Semitones},
    {P_Wave,        "Wave",         "Bass",    0, 1,  0, false, false, Unit::None},
    {P_Sub,         "Sub",          "Bass",    0, 1,  0.3, false, false, Unit::None},
    {P_FilterType,  "Filter Type",  "Bass",    0, 1,  0, true, false, Unit::None},
    {P_Cutoff,      "Cutoff",       "Bass",   40, 8000, 300, false, true, Unit::Hz},
    {P_Res,         "Resonance",    "Bass",    0, 1,  0.05, false, false, Unit::None},
    {P_EnvMod,      "Env Mod",      "Bass",    0, 6,  3, false, false, Unit::Octaves},
    {P_FDecay,      "F.Decay",      "Bass",  0.05, 1, 0.35, false, false, Unit::T16},
    {P_Attack,      "Attack",       "Bass",    0, 5,  1, false, false, Unit::Ms},
    {P_Snap,        "Snap",         "Bass",  0.2, 1,  0.6, false, false, Unit::T16},
    {P_Gate,        "Gate",         "Bass",  0.3, 0.95, 0.7, false, false, Unit::T16},
    {P_BassDrive,   "Bass Drive",   "Bass",    0, 1,  0.2, false, false, Unit::None},
    {P_Third,       "3rd-H Trim",   "Bass",   -6, 0, -2, false, false, Unit::Db},
    {P_Analog,      "Analog",       "Bass",    0, 1,  0.3, false, false, Unit::None},
}};
// clang-format on

static_assert(kParams.back().id == P_Analog && kParams.back().name != nullptr,
              "kParams size must match the number of rows");

inline constexpr std::array<int, 6> kIntervals = {0, 2, 3, 4, 5, 7};

inline const ParamInfo* findParam(uint32_t id) {
    for (const auto& p : kParams)
        if (p.id == id) return &p;
    return nullptr;
}

// Clamp (and snap, for stepped params). NaN/Inf fall back to the default.
inline double sanitizeParam(uint32_t id, double v) {
    const ParamInfo* p = findParam(id);
    if (!p) return 0.0;
    if (!std::isfinite(v)) return p->def;
    if (v < p->min) v = p->min;
    if (v > p->max) v = p->max;
    if (p->stepped) {
        v = std::floor(v + 0.5);
        if (id == P_Interval) {  // snap to allowed interval set
            int best = 0;
            for (int iv : kIntervals)
                if (std::fabs(iv - v) < std::fabs(best - v)) best = iv;
            v = best;
        }
    }
    return v;
}

struct ParamSet {
    std::array<double, kMaxParamId> v{};

    ParamSet() {
        for (const auto& p : kParams) v[p.id] = p.def;
    }
    double operator[](uint32_t id) const { return v[id < kMaxParamId ? id : 0]; }
    void set(uint32_t id, double x) {
        if (id < kMaxParamId && findParam(id)) v[id] = sanitizeParam(id, x);
    }
    bool stepOn(int step) const { return v[P_Step1On + (step - 1)] > 0.5; }
    double emph(int step) const { return v[P_Emph1 + (step - 1)]; }
};

}  // namespace kikset
