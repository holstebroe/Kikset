// Renders panel frames to PPM (design §12 gui_test) and exercises the interaction paths.
#include <cstdio>
#include <string>

#include "../gui/Panel.hpp"
#include "TestUtil.hpp"

using namespace kikset;
using namespace kikset::gui;

int main(int argc, char** argv) {
    const std::string outDir = argc > 1 ? argv[1] : ".";
    ParamSet ps;
    int gestures = 0;
    bool playing = true;
    double beat = 1.3;
    PanelHost h;
    h.get = [&](uint32_t id) { return ps[id]; };
    h.set = [&](uint32_t id, double v) { ps.set(id, v); };
    h.gesture = [&](uint32_t, bool) { ++gestures; };
    h.playing = [&] { return playing; };
    h.beatPos = [&] { return beat; };
    Panel panel(h);
    Graphics g(Panel::W, Panel::H);
    panel.render(g);
    g.writePpm((outDir + "/panel_default.ppm").c_str());

    // interaction: drag a knob up, double click to reset, toggle a step, cycle a choice
    const double sweep0 = ps[P_Sweep];
    panel.mouseDown(70 + 62 + 30, 384 + 26, 1, false, 0.0);   // SWEEP knob
    panel.mouseMove(70 + 62 + 30, 384 + 26 - 30, false);
    panel.mouseUp();
    CHECK(ps[P_Sweep] > sweep0 + 0.5, "drag up raises the knob (%g -> %g)", sweep0, ps[P_Sweep]);
    panel.mouseDown(70 + 62 + 30, 384 + 26, 1, false, 1000.0);
    panel.mouseUp();
    panel.mouseDown(70 + 62 + 30, 384 + 26, 1, false, 1100.0);
    panel.mouseUp();
    CHECK(ps[P_Sweep] == findParam(P_Sweep)->def, "double click resets");
    panel.mouseDown(14 + 78 + 10, 230, 1, false, 5000.0);
    panel.mouseUp();
    CHECK(ps[P_Step1On] < 0.5, "step 1 toggles off");
    panel.mouseDown(320, 20, 1, false, 6000.0);
    panel.mouseUp();
    CHECK(int(ps[P_Key]) == 6, "key choice cycles (%g)", ps[P_Key]);
    panel.mouseDown(320, 20, 3, false, 7000.0);
    panel.mouseUp();
    CHECK(int(ps[P_Key]) == 5, "right click cycles back");
    panel.wheel(70 + 62 * 2 + 30, 384 + 26, 3, false);
    CHECK(ps[P_Curve] > findParam(P_Curve)->def, "wheel moves knob");
    CHECK(gestures >= 8, "gesture begin/end reported");

    ps = ParamSet{};
    ps.set(P_Step1On, 0);
    ps.set(P_PhaseMode, 1);
    ps.set(P_Flow, 0.8);
    panel.render(g);
    g.writePpm((outDir + "/panel_follow_gallop.ppm").c_str());
    ps = ParamSet{};
    playing = false;
    for (int i = 0; i < 20; ++i) panel.render(g);  // cached preview: must stay fast
    return finish("gui_test");
}
