// Stand-alone editor window for trying the GUI without a host (X11).
//   kikset_gui_demo [--screenshot out.ppm] [--bpm 145]
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "../gui/X11Window.hpp"

using namespace kikset;
using namespace kikset::gui;

int main(int argc, char** argv) {
    const char* shot = nullptr;
    double bpm = 145.0;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
        else if (!std::strcmp(argv[i], "--bpm") && i + 1 < argc) bpm = std::atof(argv[++i]);
    }
    ParamSet ps;
    PanelHost h;
    h.get = [&](uint32_t id) { return ps[id]; };
    h.set = [&](uint32_t id, double v) { ps.set(id, v); };
    h.gesture = [](uint32_t, bool) {};
    h.tempo = [&] { return bpm; };
    Panel panel(h);
    X11Window win(panel, 0);
    if (!win.ok()) { std::fprintf(stderr, "cannot open X display\n"); return 1; }
    win.show();
    for (int frame = 0;; ++frame) {
        win.pump();
        if (shot && frame == 5) return win.screenshot(shot) ? 0 : 1;
        usleep(33000);
    }
}
