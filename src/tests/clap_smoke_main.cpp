// Loads the built .clap through dlopen and drives it like a minimal host:
// entry/factory, params, state round trip, processing, and (if DISPLAY is set) the X11 GUI.
#include <dlfcn.h>

#include <clap/clap.h>

#include <cstring>
#include <string>
#include <vector>

#include "TestUtil.hpp"

#ifdef KIKSET_HAS_X11
#include <X11/Xlib.h>
#endif

namespace {
int g_timerRegs = 0, g_flushReqs = 0;
const clap_host_timer_support_t kHostTimer = {
    [](const clap_host_t*, uint32_t, clap_id* id) { *id = 7; ++g_timerRegs; return true; },
    [](const clap_host_t*, clap_id) { return true; }};
const clap_host_params_t kHostParams = {[](const clap_host_t*, clap_param_rescan_flags) {},
                                        [](const clap_host_t*, clap_id, clap_param_clear_flags) {},
                                        [](const clap_host_t*) { ++g_flushReqs; }};
const void* hostExt(const clap_host_t*, const char* id) {
    if (!std::strcmp(id, CLAP_EXT_TIMER_SUPPORT)) return &kHostTimer;
    if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &kHostParams;
    return nullptr;
}
struct Stream {
    std::string data;
    size_t pos = 0;
};
}  // namespace

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "./Kikset.clap";
    void* lib = dlopen(path, RTLD_NOW);
    CHECK(lib != nullptr, "dlopen %s: %s", path, dlerror());
    if (!lib) return finish("clap_smoke");
    auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
    CHECK(entry != nullptr, "clap_entry exported");
    if (!entry) return finish("clap_smoke");
    CHECK(entry->init(path), "entry init");
    auto* fac = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    CHECK(fac && fac->get_plugin_count(fac) == 1, "factory");
    clap_host_t host{};
    host.clap_version = CLAP_VERSION;
    host.get_extension = hostExt;
    const clap_plugin_t* p = fac->create_plugin(fac, &host, "com.holstebroe.kikset");
    CHECK(p && p->init(p), "create + init");
    CHECK(p->activate(p, 48000.0, 32, 4096), "activate");

    auto* params = static_cast<const clap_plugin_params_t*>(p->get_extension(p, CLAP_EXT_PARAMS));
    CHECK(params->count(p) == 48, "param count %u", params->count(p));
    double key = -1;
    params->get_value(p, 0, &key);
    CHECK(key == 5.0, "default key is F (got %g)", key);
    for (uint32_t i = 0; i < params->count(p); ++i) {
        clap_param_info_t info;
        CHECK(params->get_info(p, i, &info) && info.min_value < info.max_value, "param %u info", i);
        CHECK(info.default_value >= info.min_value && info.default_value <= info.max_value, "param %u default", i);
    }

    // state round trip
    auto* state = static_cast<const clap_plugin_state_t*>(p->get_extension(p, CLAP_EXT_STATE));
    Stream st;
    clap_ostream_t os{&st, [](const clap_ostream_t* s, const void* b, uint64_t n) -> int64_t {
                          static_cast<Stream*>(s->ctx)->data.append(static_cast<const char*>(b), n);
                          return int64_t(n);
                      }};
    CHECK(state->save(p, &os), "state save");
    st.data += "9999=1\nnot a line\n";  // unknown id and junk are ignored
    clap_istream_t is{&st, [](const clap_istream_t* s, void* b, uint64_t n) -> int64_t {
                          auto* x = static_cast<Stream*>(s->ctx);
                          const size_t k = std::min<size_t>(n, x->data.size() - x->pos);
                          std::memcpy(b, x->data.data() + x->pos, k);
                          x->pos += k;
                          return int64_t(k);
                      }};
    CHECK(state->load(p, &is), "state load");

    // process one second of host-synced audio
    std::vector<float> l(512), r(512);
    float* ch[2] = {l.data(), r.data()};
    clap_audio_buffer_t ab{};
    ab.channel_count = 2;
    ab.data32 = ch;
    clap_event_transport_t tr{};
    tr.header.size = sizeof(tr);
    tr.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_IS_PLAYING;
    tr.tempo = 145;
    clap_input_events_t ie{nullptr, [](const clap_input_events_t*) -> uint32_t { return 0; },
                           [](const clap_input_events_t*, uint32_t) -> const clap_event_header_t* { return nullptr; }};
    clap_output_events_t oe{nullptr, [](const clap_output_events_t*, const clap_event_header_t*) { return true; }};
    clap_process_t pr{};
    pr.frames_count = 512;
    pr.audio_outputs = &ab;
    pr.audio_outputs_count = 1;
    pr.in_events = &ie;
    pr.out_events = &oe;
    pr.transport = &tr;
    CHECK(p->start_processing(p), "start processing");
    float peak = 0;
    bool finite = true;
    for (int b = 0; b < 100; ++b) {
        tr.song_pos_beats = int64_t(b * 512 * 145.0 / 60.0 / 48000.0 * CLAP_BEATTIME_FACTOR);
        p->process(p, &pr);
        for (float v : l) { peak = std::max(peak, std::fabs(v)); finite = finite && std::isfinite(v); }
    }
    CHECK(finite && peak > 0.2f, "audio produced (peak %g)", peak);

#ifdef KIKSET_HAS_X11
    if (std::getenv("DISPLAY")) {
        auto* gui = static_cast<const clap_plugin_gui_t*>(p->get_extension(p, CLAP_EXT_GUI));
        CHECK(gui != nullptr, "gui extension");
        CHECK(gui->is_api_supported(p, CLAP_WINDOW_API_X11, false), "x11 supported");
        Display* dpy = XOpenDisplay(nullptr);
        CHECK(dpy != nullptr, "X display");
        if (gui && dpy) {
            Window parent = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 900, 560, 0, 0, 0);
            XMapWindow(dpy, parent);
            XSync(dpy, False);
            CHECK(gui->create(p, CLAP_WINDOW_API_X11, false), "gui create");
            uint32_t w = 0, h = 0;
            gui->get_size(p, &w, &h);
            CHECK(w == 900 && h == 560, "gui size %ux%u", w, h);
            clap_window_t cw{};
            cw.api = CLAP_WINDOW_API_X11;
            cw.x11 = parent;
            CHECK(gui->set_parent(p, &cw), "gui set_parent");
            CHECK(gui->show(p), "gui show");
            CHECK(g_timerRegs == 1, "timer registered");
            auto* timer = static_cast<const clap_plugin_timer_support_t*>(p->get_extension(p, CLAP_EXT_TIMER_SUPPORT));
            for (int i = 0; i < 3; ++i) timer->on_timer(p, 7);
            gui->hide(p);
            gui->destroy(p);
            XDestroyWindow(dpy, parent);
            XCloseDisplay(dpy);
        }
    }
#endif
    p->stop_processing(p);
    p->deactivate(p);
    p->destroy(p);
    entry->deinit();
    return finish("clap_smoke");
}
