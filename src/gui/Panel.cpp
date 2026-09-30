#include "Panel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "../core/ParamText.hpp"

namespace kikset::gui {

namespace {
const Color kBg = rgb(22, 24, 28), kPanel = rgb(33, 36, 42), kEdge = rgb(58, 63, 72);
const Color kText = rgb(205, 210, 220), kDim = rgb(120, 126, 138);
const Color kKick = rgb(255, 140, 40), kBass = rgb(80, 220, 120), kAnchor = rgb(240, 220, 90);
const Color kPitch = rgb(130, 190, 255), kGate = rgb(255, 90, 90);
constexpr int kScopeX = 10, kScopeY = 44, kScopeW = 880, kScopeH = 160;
constexpr float kPi = 3.14159265f;

std::string fmt(const char* f, double a) { char b[64]; std::snprintf(b, sizeof b, f, a); return b; }
}  // namespace

Panel::Panel(PanelHost host) : host_(std::move(host)) {
    layout();
    kMin_.assign(kScopeW, 0); kMax_.assign(kScopeW, 0);
    bMin_.assign(kScopeW, 0); bMax_.assign(kScopeW, 0);
}

void Panel::add(Kind k, uint32_t id, int x, int y, int w, int h, const char* label) {
    ctl_.push_back({k, id, x, y, w, h, label});
}

void Panel::layout() {
    // header
    add(Kind::Choice, P_Key, 300, 10, 60, 24, "KEY");
    add(Kind::Choice, P_PlayMode, 410, 10, 96, 24, "MODE");
    add(Kind::Choice, P_PhaseMode, 560, 10, 80, 24, "PHASE");
    // step row
    add(Kind::Fixed, 0, 14, 224, 70, 40, "K");
    for (int i = 0; i < 3; ++i) add(Kind::Step, P_Step1On + i, 14 + (i + 1) * 78, 224, 70, 40, "B");
    add(Kind::Knob, P_TargetPhase, 14, 272, 62, 52, "TARGET");
    for (int i = 0; i < 3; ++i) add(Kind::Knob, P_Emph1 + i, 14 + (i + 1) * 78, 272, 62, 52, "EMPH");
    // feel row
    const uint32_t feel[] = {P_Flow, P_Roll, P_Push, P_Gel, P_LockStyle, P_Balance, P_Volume};
    const char* fl[] = {"FLOW", "ROLL", "PUSH", "GEL", "LOCK", "BAL", "VOL"};
    for (int i = 0; i < 7; ++i) add(Kind::Knob, feel[i], 340 + i * 78, 322, 62, 52, fl[i]);
    // kick
    const struct { uint32_t id; const char* n; bool choice; } kick[] = {
        {P_KickTune, "TUNE", 0}, {P_Sweep, "SWEEP", 0}, {P_Curve, "CURVE", 0}, {P_Punch, "PUNCH", 0},
        {P_Settle, "SETTLE", 0}, {P_StartPhase, "START", 0}, {P_Length, "LENGTH", 0}, {P_Shape, "SHAPE", 0},
        {P_Fishtail, "FISH", 0}, {P_ClickType, "CLICK", 1}, {P_ClickLevel, "CLK LVL", 0}};
    for (int i = 0; i < 11; ++i)
        add(kick[i].choice ? Kind::Choice : Kind::Knob, kick[i].id, 70 + i * 62, 384, 62, 52, kick[i].n);
    const struct { uint32_t id; const char* n; bool choice; } drive[] = {
        {P_KickDrive, "DRIVE", 0}, {P_SatType, "TYPE", 1}, {P_SatBias, "BIAS", 0},
        {P_SatEnvFollow, "FOLLOW", 0}, {P_SatTone, "TONE", 0}};
    for (int i = 0; i < 5; ++i)
        add(drive[i].choice ? Kind::Choice : Kind::Knob, drive[i].id, 70 + i * 62, 440, 62, 52, drive[i].n);
    const struct { uint32_t id; const char* n; bool choice; } bass[] = {
        {P_BassOct, "OCT", 1}, {P_Interval, "INTVL", 0}, {P_Wave, "WAVE", 0}, {P_Sub, "SUB", 0},
        {P_FilterType, "FILTER", 1}, {P_Cutoff, "CUTOFF", 0}, {P_Res, "RES", 0}, {P_EnvMod, "ENV MOD", 0},
        {P_FDecay, "F.DEC", 0}, {P_Attack, "ATTACK", 0}, {P_Snap, "SNAP", 0}, {P_Gate, "GATE", 0},
        {P_BassDrive, "DRIVE", 0}, {P_Third, "3RD-H", 0}, {P_Analog, "ANALOG", 0}};
    for (int i = 0; i < 15; ++i)
        add(bass[i].choice ? Kind::Choice : Kind::Knob, bass[i].id, 70 + i * 54, 496, 54, 52, bass[i].n);
}

double Panel::norm(uint32_t id, double v) const {
    const ParamInfo* p = findParam(id);
    if (!p) return 0;
    if (p->logScale) return std::log(v / p->min) / std::log(p->max / p->min);
    return (v - p->min) / (p->max - p->min);
}

double Panel::denorm(uint32_t id, double n) const {
    const ParamInfo* p = findParam(id);
    n = std::clamp(n, 0.0, 1.0);
    if (p->logScale) return p->min * std::exp(n * std::log(p->max / p->min));
    return p->min + n * (p->max - p->min);
}

int Panel::hit(int x, int y) const {
    for (int i = int(ctl_.size()) - 1; i >= 0; --i) {
        const Ctl& c = ctl_[i];
        if (c.kind == Kind::Fixed) continue;
        if (x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h) return i;
    }
    return -1;
}

void Panel::stepChoice(uint32_t id, int dir) {
    const ParamInfo* p = findParam(id);
    double v = host_.get(id);
    if (id == P_Interval) {
        int idx = 0;
        for (size_t i = 0; i < kIntervals.size(); ++i) if (kIntervals[i] == int(v + 0.5)) idx = int(i);
        idx = (idx + dir + int(kIntervals.size())) % int(kIntervals.size());
        v = kIntervals[idx];
    } else {
        const int n = int(p->max - p->min) + 1;
        v = p->min + ((int(v - p->min + 0.5) + dir + n) % n);
    }
    host_.gesture(id, true);
    host_.set(id, v);
    host_.gesture(id, false);
}

void Panel::mouseDown(int x, int y, int button, bool shift, double nowMs) {
    const int i = hit(x, y);
    if (i < 0) return;
    const Ctl& c = ctl_[i];
    const ParamInfo* p = findParam(c.id);
    if (c.kind == Kind::Step) {
        host_.gesture(c.id, true);
        host_.set(c.id, host_.get(c.id) > 0.5 ? 0.0 : 1.0);
        host_.gesture(c.id, false);
    } else if (c.kind == Kind::Choice) {
        stepChoice(c.id, button == 3 ? -1 : 1);
    } else if (c.kind == Kind::Knob) {
        if (nowMs - lastClickMs_ < 350.0 && lastClickCtl_ == i) {  // double click: reset
            host_.gesture(c.id, true);
            host_.set(c.id, p->def);
            host_.gesture(c.id, false);
            lastClickMs_ = -1e9;
            return;
        }
        drag_ = i;
        dragY_ = y;
        dragStart_ = norm(c.id, host_.get(c.id));
        host_.gesture(c.id, true);
    }
    lastClickMs_ = nowMs;
    lastClickCtl_ = i;
    (void)shift;
}

void Panel::mouseMove(int, int y, bool shift) {
    if (drag_ < 0) return;
    const Ctl& c = ctl_[drag_];
    const double per = shift ? 1000.0 : 150.0;
    double n = dragStart_ + (dragY_ - y) / per;
    host_.set(c.id, denorm(c.id, n));
}

void Panel::mouseUp() {
    if (drag_ >= 0) host_.gesture(ctl_[drag_].id, false);
    drag_ = -1;
}

void Panel::wheel(int x, int y, double steps, bool shift) {
    const int i = hit(x, y);
    if (i < 0) return;
    const Ctl& c = ctl_[i];
    if (c.kind == Kind::Knob) {
        const double n = norm(c.id, host_.get(c.id)) + steps * (shift ? 0.002 : 0.02);
        host_.set(c.id, denorm(c.id, n));
    } else if (c.kind == Kind::Choice) {
        stepChoice(c.id, steps > 0 ? 1 : -1);
    }
}

// ------------------------------------------------------------------ drawing

void Panel::refreshPreview() {
    ParamSet ps;
    for (const auto& p : kParams) ps.set(p.id, host_.get(p.id));
    const double tempo = std::clamp(host_.tempo(), 40.0, 300.0);
    if (havePreview_ && ps.v == lastSet_.v && std::fabs(tempo - lastTempo_) < 0.01) return;
    lastSet_ = ps;
    lastTempo_ = tempo;
    preview_ = renderBeatPreview(ps, tempo, int(ps[P_Key]), 48000.0);
    havePreview_ = true;
    const size_t n = preview_.mix.size();
    float peak = 0.3f;
    for (int c = 0; c < kScopeW; ++c) {
        const size_t a = n * c / kScopeW, b = std::max(a + 1, n * (c + 1) / kScopeW);
        float k0 = 0, k1 = 0, b0 = 0, b1 = 0;
        for (size_t i = a; i < b && i < n; ++i) {
            k0 = std::min(k0, preview_.kick[i]); k1 = std::max(k1, preview_.kick[i]);
            b0 = std::min(b0, preview_.bass[i]); b1 = std::max(b1, preview_.bass[i]);
        }
        kMin_[c] = k0; kMax_[c] = k1; bMin_[c] = b0; bMax_[c] = b1;
        peak = std::max({peak, -k0, k1, -b0, b1});
    }
    scopePeak_ = peak * 1.08f;
}

void Panel::drawKnob(Graphics& g, const Ctl& c) {
    const double v = host_.get(c.id);
    const ParamInfo* p = findParam(c.id);
    const double n = norm(c.id, v);
    const float cx = c.x + c.w / 2.f, cy = c.y + 26.f, r = 13.f;
    g.text(int(cx), c.y + 2, c.label, kDim, 1, 1);
    const float a0 = kPi * 0.75f, a1 = kPi * 2.25f;
    g.arc(cx, cy, r, a0, a1, kEdge, 2.f);
    // bipolar params draw from the middle
    const bool bip = p->min < 0 && p->max > 0 && std::fabs(p->min + p->max) < 1e-9;
    const float an = a0 + float(n) * (a1 - a0);
    const Color col = c.id >= P_BassOct ? kBass : (c.id >= P_KickTune ? kKick : kPitch);
    if (bip) g.arc(cx, cy, r, std::min(an, (a0 + a1) / 2), std::max(an, (a0 + a1) / 2), col, 2.f);
    else g.arc(cx, cy, r, a0, an, col, 2.f);
    g.line(cx + std::cos(an) * 4, cy + std::sin(an) * 4, cx + std::cos(an) * (r - 1), cy + std::sin(an) * (r - 1),
           kText);
    g.text(int(cx), c.y + 42, paramText(c.id, v), kText, 1, 1);
}

void Panel::drawChoice(Graphics& g, const Ctl& c) {
    const bool header = c.y < 40;
    const int bx = c.x + (header ? 0 : 4), bw = c.w - (header ? 0 : 8);
    const int by = header ? c.y : c.y + 18, bh = header ? c.h : 18;
    if (header) g.text(c.x - 6, c.y + 8, c.label, kDim, 1, 2);
    else g.text(c.x + c.w / 2, c.y + 2, c.label, kDim, 1, 1);
    g.fillRect(bx, by, bw, bh, kPanel);
    g.rect(bx, by, bw, bh, kEdge);
    g.text(bx + bw / 2, by + (bh - 7) / 2, paramText(c.id, host_.get(c.id)), kText, 1, 1);
}

void Panel::drawStep(Graphics& g, const Ctl& c, int idx) {
    const bool on = c.kind == Kind::Fixed || host_.get(c.id) > 0.5;
    const Color col = c.kind == Kind::Fixed ? kKick : kBass;
    g.fillRect(c.x, c.y, c.w, c.h, on ? rgb(col >> 16 & 255, col >> 8 & 255, col & 255) : kPanel, on ? 0.30f : 1.f);
    g.rect(c.x, c.y, c.w, c.h, on ? col : kEdge);
    // playhead light
    if (host_.playing()) {
        const double ph = host_.beatPos() - std::floor(host_.beatPos());
        if (int(ph * 4) == idx) g.fillRect(c.x + 2, c.y + 2, c.w - 4, 3, kText);
    }
    std::string s = c.kind == Kind::Fixed ? "KICK" : std::string("BASS ") + char('0' + idx);
    g.text(c.x + c.w / 2, c.y + 17, s, on ? kText : kDim, 1, 1);
}

void Panel::drawScope(Graphics& g) {
    const int x0 = kScopeX, y0 = kScopeY, w = kScopeW, h = kScopeH;
    g.fillRect(x0, y0, w, h, kPanel);
    g.rect(x0 - 1, y0 - 1, w + 2, h + 2, kEdge);
    const int py = y0 + 14, ph = h - 16, cy = py + ph / 2;
    const BeatPlan& pl = preview_.plan;
    const double Tb = pl.Tbeat;
    auto tx = [&](double t) { return x0 + float(t / Tb * w); };

    // lanes
    const char* names[4] = {"K", "B1", "B2", "B3"};
    for (int i = 0; i < 4; ++i) {
        if (i) g.fillRect(x0 + i * w / 4, y0, 1, h, kEdge);
        const bool on = i == 0 || pl.notes[i - 1].active;
        g.text(x0 + i * w / 4 + 5, y0 + 4, names[i], on ? (i ? kBass : kKick) : kDim);
    }
    g.fillRect(x0, cy, w, 1, kEdge, 0.6f);

    const float sc = (ph / 2.f - 2.f) / scopePeak_;
    for (int c = 0; c < w; ++c) {
        g.line(x0 + c + 0.5f, cy - bMax_[c] * sc, x0 + c + 0.5f, cy - bMin_[c] * sc - 0.01f, kBass, 0.85f);
        g.line(x0 + c + 0.5f, cy - kMax_[c] * sc, x0 + c + 0.5f, cy - kMin_[c] * sc - 0.01f, kKick, 0.85f);
    }

    // pitch curve (log axis 30 Hz .. 1.5 kHz)
    const double tau = pl.kick.shape.tau;
    auto fy = [&](double f) {
        const double u = std::clamp(std::log(f / 30.0) / std::log(1500.0 / 30.0), 0.0, 1.0);
        return float(py + ph - u * ph);
    };
    float px = 0, pyy = 0;
    for (int c = 0; c < w; c += 2) {
        const double t = double(c) / w * Tb;
        if (t < tau) continue;
        const float y = fy(pl.kick.model.freq(t - tau)), x = x0 + c;
        if (px > 0) g.line(px, pyy, x, y, kPitch, 0.8f);
        px = x; pyy = y;
    }
    for (double f : {50.0, 100.0, 200.0, 500.0, 1000.0}) {
        g.fillRect(x0 + w - 4, int(fy(f)), 4, 1, kPitch);
        g.text(x0 + w - 7, int(fy(f)) - 3, fmt("%.0f", f), kPitch, 1, 2);
    }
    const double settle = tau + pl.kick.model.settleTime();
    g.fillCircle(tx(settle), fy(pl.f0), 3.f, kPitch);

    // anchors
    for (int i = 0; i < 3; ++i) {
        const auto& n = pl.notes[i];
        if (!n.active) continue;
        const float x = tx(n.onset);
        for (int y = py; y < py + ph; y += 4) g.fillRect(int(x), y, 1, 2, kAnchor, 0.8f);
        g.text(int(x) + 3, y0 + h - 22, fmt("%+.1f DB", pl.flowGainDb[i]), kBass);
        if (i == pl.firstActive) {
            const double ta = n.onset - tau;
            const double resid = wrapHalf(pl.kick.model.phase(ta) - pl.notes[i].phase0) * 360.0;
            g.text(int(x) + 3, y0 + h - 12, fmt("ERR %.1f DEG", std::fabs(resid) < 0.05 ? 0.0 : resid), kAnchor);
        }
    }
    // tail gate
    if (!pl.followMode && pl.firstActive >= 0) {
        const float x = tx(tau + pl.kick.amp.gateEnd);
        g.line(x, py + ph - 8, x - 4, py + ph, kGate);
        g.line(x, py + ph - 8, x + 4, py + ph, kGate);
        g.line(x - 4, py + ph, x + 4, py + ph, kGate);
    }
    // playhead
    if (host_.playing()) {
        const double b = host_.beatPos();
        g.fillRect(x0 + int((b - std::floor(b)) * w), y0, 1, h, rgb(255, 255, 255), 0.7f);
    }
}

void Panel::drawHandoff(Graphics& g) {
    const int x0 = 340, y0 = 224, w = 330, h = 88;
    g.fillRect(x0, y0, w, h, kPanel);
    g.rect(x0, y0, w, h, kEdge);
    g.text(x0 + 4, y0 + 3, "HANDOFF +-3 MS AT FIRST ANCHOR", kDim);
    const BeatPlan& pl = preview_.plan;
    const int fs = 48000;
    if (pl.firstActive < 0) { g.text(x0 + w / 2, y0 + h / 2, "NO BASS STEP", kDim, 1, 1); return; }
    const double ta = pl.notes[pl.firstActive].onset;
    const long c = long(ta * fs), half = long(0.003 * fs);
    const int cy = y0 + 14 + (h - 18) / 2;
    g.fillRect(x0 + w / 2, y0 + 12, 1, h - 14, kAnchor, 0.6f);
    g.fillRect(x0, cy, w, 1, kEdge, 0.6f);
    float peak = 0.05f;
    for (long i = c - half; i <= c + half; ++i)
        if (i >= 0 && i < long(preview_.kick.size())) peak = std::max({peak, std::fabs(preview_.kick[i]), std::fabs(preview_.bass[i])});
    const float sc = (h - 22) / 2.f / peak;
    for (int pass = 0; pass < 2; ++pass) {
        const auto& v = pass ? preview_.kick : preview_.bass;
        float px = 0, pyy = 0;
        for (long i = c - half; i <= c + half; ++i) {
            if (i < 0 || i >= long(v.size())) continue;
            const float x = x0 + float(i - (c - half)) / (2 * half) * w, y = cy - v[i] * sc;
            if (i > c - half) g.line(px, pyy, x, y, pass ? kKick : kBass);
            px = x; pyy = y;
        }
    }
}

void Panel::render(Graphics& g) {
    refreshPreview();
    g.clear(kBg);
    g.text(14, 10, "KIKSET", kKick, 3);
    g.text(14, 34, "KICK + BASS PHASE LOCK", kDim);
    for (size_t i = 0; i < ctl_.size(); ++i) {
        const Ctl& c = ctl_[i];
        switch (c.kind) {
            case Kind::Knob: drawKnob(g, c); break;
            case Kind::Choice: drawChoice(g, c); break;
            case Kind::Step: drawStep(g, c, c.id - P_Step1On + 1); break;
            case Kind::Fixed: drawStep(g, c, 0); break;
        }
    }
    drawScope(g);
    drawHandoff(g);
    // solver read-outs
    const BeatPlan& pl = preview_.plan;
    const auto& s = pl.solve;
    g.text(680, 228, "SOLVER", kAnchor);
    g.text(680, 242, fmt("NUDGE %.2f MS", s.nudgeMs), kText);
    g.text(680, 254, fmt("D PITCH %.0f C", s.dPitchCents), kText);
    g.text(680, 266, fmt("D PHASE %.0f DEG", s.dPhaseDeg), kText);
    g.text(680, 278, fmt("F0 %.1f HZ", pl.f0), kText);
    g.text(680, 290, s.converged ? "LOCKED" : "NOT LOCKED", s.converged ? kBass : kGate);
    g.text(680, 302, fmt("RESID %.0E CYC", std::fabs(s.residual)), kDim);
    g.text(14, 334, "EMPHASIS AND STEP FEEL", kDim);
    g.text(14, 346, "FLOW SHOWS AS DB IN SCOPE", kDim);
    // section labels
    g.text(10, 398, "KICK", kKick);
    g.text(10, 454, "DRIVE", kKick);
    g.text(10, 510, "BASS", kBass);
}

}  // namespace kikset::gui
