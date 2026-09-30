// CLAP entry, factory, extensions and param/state/event glue.
#include <clap/clap.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "../core/KiksetEngine.hpp"
#include "../core/ParamText.hpp"
#ifdef KIKSET_HAS_X11
#include "../gui/X11Window.hpp"
#endif

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

    // GUI (main thread) -> audio thread edits; single producer, single consumer.
    struct GuiMsg { uint32_t id; double value; int type; };  // 0 value, 1 gesture begin, 2 gesture end
    static constexpr uint32_t kQ = 1024;
    GuiMsg queue[kQ];
    std::atomic<uint32_t> qHead{0}, qTail{0};
    std::atomic<double> tempo{145.0};
    std::atomic<bool> playing{false};
    bool guiPush(const GuiMsg& m) {
        const uint32_t t = qTail.load(std::memory_order_relaxed), n = (t + 1) % kQ;
        if (n == qHead.load(std::memory_order_acquire)) return false;
        queue[t] = m;
        qTail.store(n, std::memory_order_release);
        return true;
    }
#ifdef KIKSET_HAS_X11
    struct Gui;
    Gui* gui = nullptr;
#endif
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

bool paramsValueToText(const clap_plugin_t*, clap_id id, double v, char* out, uint32_t n) {
    if (!findParam(id)) return false;
    std::snprintf(out, n, "%s", paramText(id, v).c_str());
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* text, double* out) {
    const ParamInfo* p = findParam(id);
    if (!p) return false;
    if (id == P_Key) {
        for (int i = 0; i < 12; ++i)
            if (strcasecmp(text, keyNames()[i]) == 0) { *out = i; return true; }
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

// Applies queued GUI edits to the engine and reports them to the host.
void drainGui(Plugin* pl, const clap_output_events_t* out) {
    for (;;) {
        const uint32_t h = pl->qHead.load(std::memory_order_relaxed);
        if (h == pl->qTail.load(std::memory_order_acquire)) break;
        const Plugin::GuiMsg m = pl->queue[h];
        pl->qHead.store((h + 1) % Plugin::kQ, std::memory_order_release);
        if (m.type == 0) pl->engine.setParam(m.id, m.value);
        if (!out) continue;
        if (m.type == 0) {
            clap_event_param_value_t e{};
            e.header = {sizeof(e), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, CLAP_EVENT_IS_LIVE};
            e.param_id = m.id;
            e.cookie = nullptr;
            e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
            e.value = m.value;
            out->try_push(out, &e.header);
        } else {
            clap_event_param_gesture_t e{};
            e.header = {sizeof(e), 0, CLAP_CORE_EVENT_SPACE_ID,
                        uint16_t(m.type == 1 ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END),
                        CLAP_EVENT_IS_LIVE};
            e.param_id = m.id;
            out->try_push(out, &e.header);
        }
    }
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t* out) {
    Plugin* pl = P(plugin);
    applyPending(pl);
    drainGui(pl, out);
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


// --------------------------------------------------------------------- gui
#ifdef KIKSET_HAS_X11
struct Plugin::Gui {
    std::unique_ptr<gui::Panel> panel;
    std::unique_ptr<gui::X11Window> win;
    clap_id timer = CLAP_INVALID_ID;
};

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool floating) {
    return !floating && !std::strcmp(api, CLAP_WINDOW_API_X11);
}
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* floating) {
    *api = CLAP_WINDOW_API_X11;
    *floating = false;
    return true;
}
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool floating) {
    Plugin* pl = P(plugin);
    if (!guiIsApiSupported(plugin, api, floating) || pl->gui) return false;
    auto* g = new Plugin::Gui();
    gui::PanelHost h;
    h.get = [pl](uint32_t id) { return pl->values[id < kMaxParamId ? id : 0].load(std::memory_order_relaxed); };
    h.set = [pl](uint32_t id, double v) {
        v = sanitizeParam(id, v);
        pl->values[id].store(v, std::memory_order_relaxed);
        pl->guiPush({id, v, 0});
        if (auto* ext = static_cast<const clap_host_params_t*>(pl->host->get_extension(pl->host, CLAP_EXT_PARAMS)))
            ext->request_flush(pl->host);
    };
    h.gesture = [pl](uint32_t id, bool begin) {
        pl->guiPush({id, 0.0, begin ? 1 : 2});
        if (auto* ext = static_cast<const clap_host_params_t*>(pl->host->get_extension(pl->host, CLAP_EXT_PARAMS)))
            ext->request_flush(pl->host);
    };
    h.tempo = [pl] { return pl->tempo.load(std::memory_order_relaxed); };
    h.beatPos = [pl] { return pl->engine.beatPosition.load(std::memory_order_relaxed); };
    h.playing = [pl] { return pl->playing.load(std::memory_order_relaxed); };
    g->panel = std::make_unique<gui::Panel>(std::move(h));
    pl->gui = g;
    return true;
}
void guiDestroy(const clap_plugin_t* plugin) {
    Plugin* pl = P(plugin);
    if (!pl->gui) return;
    if (pl->gui->timer != CLAP_INVALID_ID)
        if (auto* t = static_cast<const clap_host_timer_support_t*>(pl->host->get_extension(pl->host, CLAP_EXT_TIMER_SUPPORT)))
            t->unregister_timer(pl->host, pl->gui->timer);
    delete pl->gui;
    pl->gui = nullptr;
}
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) {
    *w = gui::Panel::W;
    *h = gui::Panel::H;
    return true;
}
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) {
    *w = gui::Panel::W;
    *h = gui::Panel::H;
    return true;
}
bool guiSetSize(const clap_plugin_t*, uint32_t w, uint32_t h) { return w == gui::Panel::W && h == gui::Panel::H; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window) {
    Plugin* pl = P(plugin);
    if (!pl->gui || std::strcmp(window->api, CLAP_WINDOW_API_X11) != 0) return false;
    pl->gui->win = std::make_unique<gui::X11Window>(*pl->gui->panel, window->x11);
    return pl->gui->win->ok();
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return true; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) {
    Plugin* pl = P(plugin);
    if (!pl->gui || !pl->gui->win) return false;
    pl->gui->win->show();
    if (pl->gui->timer == CLAP_INVALID_ID)
        if (auto* t = static_cast<const clap_host_timer_support_t*>(pl->host->get_extension(pl->host, CLAP_EXT_TIMER_SUPPORT)))
            t->register_timer(pl->host, 33, &pl->gui->timer);
    return true;
}
bool guiHide(const clap_plugin_t* plugin) {
    Plugin* pl = P(plugin);
    if (!pl->gui || !pl->gui->win) return false;
    pl->gui->win->hide();
    return true;
}
const clap_plugin_gui_t kGuiExt = {guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale,
                                   guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize,
                                   guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide};

void timerOnTimer(const clap_plugin_t* plugin, clap_id id) {
    Plugin* pl = P(plugin);
    if (pl->gui && pl->gui->win && id == pl->gui->timer) pl->gui->win->pump();
}
const clap_plugin_timer_support_t kTimerExt = {timerOnTimer};
#endif

// ------------------------------------------------------------------ plugin
bool init(const clap_plugin_t* plugin) {
    Plugin* pl = P(plugin);
    for (const auto& p : kParams) pl->values[p.id].store(p.def);
    for (auto& d : pl->pendingDirty) d.store(false);
    return true;
}

void destroy(const clap_plugin_t* plugin) {
#ifdef KIKSET_HAS_X11
    guiDestroy(plugin);
#endif
    delete P(plugin);
}

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
    drainGui(pl, pr->out_events);
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
    pl->tempo.store(tr.tempo, std::memory_order_relaxed);
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
        pl->playing.store(tr.playing, std::memory_order_relaxed);
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
#ifdef KIKSET_HAS_X11
    if (!std::strcmp(id, CLAP_EXT_GUI)) return &kGuiExt;
    if (!std::strcmp(id, CLAP_EXT_TIMER_SUPPORT)) return &kTimerExt;
#endif
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
