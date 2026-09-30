// Display text for parameter values; shared by the CLAP glue and the GUI.
#pragma once
#include <cstdio>
#include <string>

#include "Params.hpp"

namespace kikset {

inline const char* const* keyNames() {
    static const char* const n[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return n;
}

// Names for stepped choice params; nullptr if the param is not a named choice.
inline const char* choiceName(uint32_t id, double v) {
    static const char* const sat[] = {"Soft", "Tube", "Diode", "Tape"};
    static const char* const click[] = {"Tick", "Tok", "Snap", "Tap", "FM", "Noise", "909", "HiTech"};
    const int i = int(v + 0.5);
    switch (id) {
        case P_Key: return keyNames()[i % 12];
        case P_PlayMode: return i ? "MIDI Gate" : "Host";
        case P_PhaseMode: return i ? "Follow" : "Reset";
        case P_SatType: return sat[i & 3];
        case P_FilterType: return i ? "OTA 12" : "Ladder 24";
        case P_Step1On: case P_Step2On: case P_Step3On: return i ? "On" : "Off";
        case P_BassOct: return i ? "+1" : "0";
        case P_ClickType: return click[i & 7];
        default: return nullptr;
    }
}

inline std::string paramText(uint32_t id, double v) {
    const ParamInfo* p = findParam(id);
    if (!p) return "";
    v = sanitizeParam(id, v);
    if (const char* c = choiceName(id, v)) return c;
    const char* u = "";
    switch (p->unit) {
        case Unit::Ms: u = " ms"; break;
        case Unit::Degrees: u = " deg"; break;
        case Unit::Db: u = " dB"; break;
        case Unit::Hz: u = " Hz"; break;
        case Unit::Octaves: u = " oct"; break;
        case Unit::Semitones: u = " st"; break;
        case Unit::T16: u = " T16"; break;
        default: break;
    }
    char b[48];
    if (p->stepped) std::snprintf(b, sizeof b, "%d%s", int(v), u);
    else if (p->unit == Unit::Hz) std::snprintf(b, sizeof b, v >= 1000 ? "%.2fk" : "%.0f", v >= 1000 ? v / 1000 : v);
    else if (p->unit == Unit::Db && v <= p->min + 0.01) std::snprintf(b, sizeof b, "-inf");
    else std::snprintf(b, sizeof b, "%.2f%s", v, u);
    return b;
}

}  // namespace kikset
