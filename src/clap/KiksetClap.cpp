// CLAP entry, factory, extensions and param/state/event glue.
#include <clap/clap.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "../core/KiksetEngine.hpp"

using namespace kikset;

namespace {

struct Plugin {
    clap_plugin_t plugin;
    const clap_host_t* host = nullptr;
    KiksetEngine engine;
    double sampleRate = 48000.0;
    std::atomic<double> values[kMaxParamId];  // last known value per id (main-thread readable)
    std::atomic<double> pending[kMaxParamId];  // state-load -> audio
    std::atomic<bool> pendingDirty[kMaxParamId];
    std::atomic<bool> anyPending{false};
};

Plugin* P(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }

// ------------------------------------------------------------------ params
uint32_t paramsCount(const clap_plugin_t*) { return uint32_t(kParams.size()); }

bool paramsGetInfo(const clap_plugin_t*, uint32_t i, clap_param_info_t* info) {
    if (i >= kParams.size()) return false;
    const ParamInfo& p = kParams[i];
    std::memset(info, 0, sizeof(*info));
    info->id = p.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (p.stepped ? CLAP_PARAM_IS_STEPPED : 0);
    std::snprintf(info->name, sizeof(info->name), "%s", p.name);
    std::snprintf(info->module, sizeof(info->module), "%s", p.module);
    info->min_value = p.min;
    info->max_value = p.max;
    info->default_value = p.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* pl, clap_id id, double* v) {
    if (id >= kMaxParamId || !findParam(id)) return false;
    *v = P(pl)->values[id].load(std::memory_order_relaxed);
    return true;
}

const char* const kKeyNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char* const kSatNames[] = {"Soft", "Tube", "Diode", "Tape"};

bool paramsValueToText(const clap_plugin_t*, clap_id id, double v, char* out, uint32_t n) {
    const ParamInfo* p = findParam(id);
    if (!p) return false;
    v = sanitizeParam(id, v);
    switch (id) {
        case P_Key: std::snprintf(out, n, "%s", kKeyNames[int(v)]); return true;
        case P_PlayMode: std::snprintf(out, n, "%s", v > 0.5 ? "MIDI Gate" : "Host"); return true;
        case P_PhaseMode: std::snprintf(out, n, "%s", v > 0.5 ? "Follow" : "Reset"); return true;
        case P_SatType: std::snprintf(out, n, "%s", kSatNames[int(v)]); return true;
        case P_FilterType: std::snprintf(out, n, "%s", v > 0.5 ? "OTA 12" : "Ladder 24"); return true;
        case P_Step1On: case P_Step2On: case P_Step3On:
            std::snprintf(out, n, "%s", v > 0.5 ? "On" : "Off"); return true;
        default: break;
    }
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
    if (p->stepped) std::snprintf(out, n, "%d%s", int(v), u);
    else std::snprintf(out, n, "%.2f%s", v, u);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* text, double* out) {
    const ParamInfo* p = findParam(id);
    if (!p) return false;
    if (id == P_Key) {
        for (int i = 0; i < 12; ++i)
            if (strcasecmp(text, kKeyNames[i]) == 0) { *out = i; return true; }
    }
    char* end = nullptr;
    const double v = std::strtod(text, &end);
    if (end == text) return false;
    *out = v;
    return true;
}

void applyEvent(Plugin* pl, const clap_event_header_t* h) {
    if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
    switch (h->type) {
        case CLAP_EVENT_PARAM_VALUE: {
            auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
            if (e->param_id < kMaxParamId && findParam(e->param_id)) {
                pl->engine.setParam(e->param_id, e->value);
                pl->values[e->param_id].store(pl->engine.params()[e->param_id], std::memory_order_relaxed);
            }
            break;
        }
        case CLAP_EVENT_NOTE_ON: {
            auto* e = reinterpret_cast<const clap_event_note_t*>(h);
            if (e->velocity > 0.0) pl->engine.noteOn(e->key); else pl->engine.noteOff(e->key);
            break;
        }
        case CLAP_EVENT_NOTE_OFF:
        case CLAP_EVENT_NOTE_CHOKE: {
            auto* e = reinterpret_cast<const clap_event_note_t*>(h);
            pl->engine.noteOff(e->key);
            break;
        }
        case CLAP_EVENT_MIDI: {
            auto* e = reinterpret_cast<const clap_event_midi_t*>(h);
            const uint8_t st = e->data[0] & 0xF0;
            if (st == 0x90 && e->data[2] > 0) pl->engine.noteOn(e->data[1]);
            else if (st == 0x80 || st == 0x90) pl->engine.noteOff(e->data[1]);
            else if (st == 0xB0 && (e->data[1] == 123 || e->data[1] == 120)) pl->engine.allNotesOff();
            break;
        }
        default: break;
    }
}

void applyPending(Plugin* pl) {
    if (!pl->anyPending.exchange(false)) return;
    for (uint32_t id = 0; id < kMaxParamId; ++id)
        if (pl->pendingDirty[id].exchange(false)) {
            pl->engine.setParam(id, pl->pending[id].load());
            pl->values[id].store(pl->engine.params()[id]);
        }
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) {
    Plugin* pl = P(plugin);
    applyPending(pl);
    for (uint32_t i = 0; i < in->size(in); ++i) applyEvent(pl, in->get(in, i));
}

const clap_plugin_params_t kParamsExt = {paramsCount, paramsGetInfo, paramsGetValue,
                                         paramsValueToText, paramsTextToValue, paramsFlush};

// ------------------------------------------------------------------- state
constexpr const char* kStateMagic = "KIKSET1\n";

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* s) {
    Plugin* pl = P(plugin);
    std::string out = kStateMagic;
    char line[64];
    for (const auto& p : kParams) {
        std::snprintf(line, sizeof(line), "%u=%.9g\n", p.id, pl->values[p.id].load());
        out += line;
    }
    const char* d = out.data();
    int64_t left = int64_t(out.size());
    while (left > 0) {
        const int64_t w = s->write(s, d, uint64_t(left));
        if (w <= 0) return false;
        d += w;
        left -= w;
    }
    return true;
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* s) {
    Plugin* pl = P(plugin);
    std::string data;
    char buf[512];
    for (;;) {
        const int64_t r = s->read(s, buf, sizeof(buf));
        if (r < 0) return false;
        if (r == 0) break;
        data.append(buf, size_t(r));
        if (data.size() > (1u << 20)) return false;
    }
    if (data.compare(0, std::strlen(kStateMagic), kStateMagic) != 0) return false;
    size_t pos = std::strlen(kStateMagic);
    while (pos < data.size()) {
        size_t eol = data.find('\n', pos);
        if (eol == std::string::npos) eol = data.size();
        const std::string ln = data.substr(pos, eol - pos);
        pos = eol + 1;
        const size_t eq = ln.find('=');
        if (eq == std::string::npos) continue;
        char* e1 = nullptr;
        const unsigned long id = std::strtoul(ln.c_str(), &e1, 10);
        if (e1 == ln.c_str() || id >= kMaxParamId || !findParam(uint32_t(id))) continue;  // unknown ids ignored
        const double v = std::strtod(ln.c_str() + eq + 1, nullptr);
        pl->pending[id].store(sanitizeParam(uint32_t(id), v));
        pl->pendingDirty[id].store(true);
        pl->values[id].store(sanitizeParam(uint32_t(id), v));
    }
    pl->anyPending.store(true);
    return true;
}

const clap_plugin_state_t kStateExt = {stateSave, stateLoad};

// ------------------------------------------------------------- audio / notes
uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 0 : 1; }
bool audioPortsGet(const clap_plugin_t*, uint32_t i, bool isInput, clap_audio_port_info_t* info) {
    if (isInput || i != 0) return false;
    info->id = 0;
    std::snprintf(info->name, sizeof(info->name), "Main");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t kAudioPortsExt = {audioPortsCount, audioPortsGet};

uint32_t notePortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 1 : 0; }
bool notePortsGet(const clap_plugin_t*, uint32_t i, bool isInput, clap_note_port_info_t* info) {
    if (!isInput || i != 0) return false;
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::snprintf(info->name, sizeof(info->name), "Notes");
    return true;
}
const clap_plugin_note_ports_t kNotePortsExt = {notePortsCount, notePortsGet};

uint32_t latencyGet(const clap_plugin_t*) { return 0; }
const clap_plugin_latency_t kLatencyExt = {latencyGet};

// ------------------------------------------------------------------ plugin
bool init(const clap_plugin_t* plugin) {
    Plugin* pl = P(plugin);
    for (const auto& p : kParams) pl->values[p.id].store(p.def);
    for (auto& d : pl->pendingDirty) d.store(false);
    return true;
}

void destroy(const clap_plugin_t* plugin) { delete P(plugin); }

bool activate(const clap_plugin_t* plugin, double sr, uint32_t, uint32_t) {
    Plugin* pl = P(plugin);
    pl->sampleRate = sr;
    pl->engine.setSampleRate(sr);
    for (const auto& p : kParams) pl->engine.setParam(p.id, pl->values[p.id].load());
    return true;
}
void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void resetPlugin(const clap_plugin_t* plugin) { P(plugin)->engine.reset(); }

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* pr) {
    Plugin* pl = P(plugin);
    applyPending(pl);
    if (pr->audio_outputs_count < 1 || pr->audio_outputs[0].channel_count < 2) return CLAP_PROCESS_ERROR;
    float* L = pr->audio_outputs[0].data32[0];
    float* R = pr->audio_outputs[0].data32[1];

    Transport tr;
    tr.tempo = 145.0;
    bool haveBeats = false;
    if (pr->transport) {
        const auto* t = pr->transport;
        tr.playing = (t->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
        if (t->flags & CLAP_TRANSPORT_HAS_TEMPO) tr.tempo = t->tempo;
        if (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) {
            tr.beatPos = double(t->song_pos_beats) / double(CLAP_BEATTIME_FACTOR);
            haveBeats = true;
        }
    }
    if (!haveBeats) tr.playing = false;  // no beat grid -> host mode idles (MIDI Gate self-clocks)

    const uint32_t nFrames = pr->frames_count;
    const uint32_t nEv = pr->in_events ? pr->in_events->size(pr->in_events) : 0;
    uint32_t pos = 0, ei = 0;
    while (pos < nFrames) {
        uint32_t next = nFrames;
        // apply all events due at `pos`, then render up to the next event time
        while (ei < nEv) {
            const clap_event_header_t* h = pr->in_events->get(pr->in_events, ei);
            if (h->time > pos) { next = std::min<uint32_t>(h->time, nFrames); break; }
            if (h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_TRANSPORT) {
                const auto* t = reinterpret_cast<const clap_event_transport_t*>(h);
                tr.playing = (t->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
                if (t->flags & CLAP_TRANSPORT_HAS_TEMPO) tr.tempo = t->tempo;
                if (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) {
                    tr.beatPos = double(t->song_pos_beats) / double(CLAP_BEATTIME_FACTOR);
                    haveBeats = true;
                }
            } else {
                applyEvent(pl, h);
            }
            ++ei;
        }
        if (next <= pos) next = nFrames;
        pl->engine.process(L + pos, R + pos, next - pos, tr);
        if (tr.playing) tr.beatPos += double(next - pos) * tr.tempo / 60.0 / pl->sampleRate;
        pos = next;
    }
    return CLAP_PROCESS_CONTINUE;
}

const void* getExtension(const clap_plugin_t*, const char* id) {
    if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &kParamsExt;
    if (!std::strcmp(id, CLAP_EXT_STATE)) return &kStateExt;
    if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &kAudioPortsExt;
    if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS)) return &kNotePortsExt;
    if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &kLatencyExt;
    return nullptr;
}
void onMainThread(const clap_plugin_t*) {}

const char* const kFeatures[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER,
                                 CLAP_PLUGIN_FEATURE_DRUM, CLAP_PLUGIN_FEATURE_MONO, nullptr};

const clap_plugin_descriptor_t kDescriptor = {
    CLAP_VERSION, "com.holstebroe.kikset", "Kikset", "holstebroe", "https://github.com/holstebroe/Kikset", "",
    "", "0.1.0", "Phase-locked kick + bass synthesizer for psytrance / Goa", kFeatures};

const clap_plugin_t* createPlugin(const clap_host_t* host) {
    auto* pl = new Plugin();
    pl->host = host;
    pl->plugin = {&kDescriptor, pl, init, destroy, activate, deactivate, startProcessing,
                  stopProcessing, resetPlugin, process, getExtension, onMainThread};
    return &pl->plugin;
}

uint32_t factoryCount(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* factoryDescriptor(const clap_plugin_factory_t*, uint32_t i) {
    return i == 0 ? &kDescriptor : nullptr;
}
const clap_plugin_t* factoryCreate(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
    if (!host || !id || std::strcmp(id, kDescriptor.id) != 0) return nullptr;
    return createPlugin(host);
}
const clap_plugin_factory_t kFactory = {factoryCount, factoryDescriptor, factoryCreate};

bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* id) {
    return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFactory : nullptr;
}

}  // namespace

extern "C" {
CLAP_EXPORT const clap_plugin_entry_t clap_entry = {CLAP_VERSION, entryInit, entryDeinit, entryGetFactory};
}
