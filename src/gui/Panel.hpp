// The Kikset editor panel (design §10): pure drawing + interaction, no OS or CLAP code.
#pragma once
#include <functional>
#include <vector>

#include "../core/KiksetEngine.hpp"
#include "Graphics.hpp"

namespace kikset::gui {

struct PanelHost {
    std::function<double(uint32_t)> get;            // current param value
    std::function<void(uint32_t, double)> set;      // user edit
    std::function<void(uint32_t, bool)> gesture;    // begin / end
    std::function<double()> tempo = [] { return 145.0; };
    std::function<double()> beatPos = [] { return 0.0; };
    std::function<bool()> playing = [] { return false; };
};

class Panel {
public:
    static constexpr int W = 900, H = 560;
    explicit Panel(PanelHost host);

    void render(Graphics& g);
    void mouseDown(int x, int y, int button, bool shift, double nowMs);
    void mouseMove(int x, int y, bool shift);
    void mouseUp();
    void wheel(int x, int y, double steps, bool shift);

private:
    enum class Kind { Knob, Choice, Step, Fixed };
    struct Ctl {
        Kind kind;
        uint32_t id;
        int x, y, w, h;  // cell
        const char* label;
    };
    void add(Kind k, uint32_t id, int x, int y, int w, int h, const char* label);
    void layout();
    int hit(int x, int y) const;
    double norm(uint32_t id, double v) const;
    double denorm(uint32_t id, double n) const;
    void refreshPreview();
    void drawKnob(Graphics&, const Ctl&);
    void drawChoice(Graphics&, const Ctl&);
    void drawStep(Graphics&, const Ctl&, int idx);
    void drawScope(Graphics&);
    void drawHandoff(Graphics&);
    void stepChoice(uint32_t id, int dir);

    PanelHost host_;
    std::vector<Ctl> ctl_;
    int drag_ = -1;
    int dragY_ = 0;
    double dragStart_ = 0.0;
    double lastClickMs_ = -1e9;
    int lastClickCtl_ = -1;

    ParamSet lastSet_;
    double lastTempo_ = 0.0;
    bool havePreview_ = false;
    BeatPreview preview_;
    std::vector<float> kMin_, kMax_, bMin_, bMax_;
    float scopePeak_ = 0.5f;
};

}  // namespace kikset::gui
