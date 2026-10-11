#include "Panel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "../core/ParamText.hpp"

namespace kikset::gui {

namespace {
// Modern GPU/Plugin Dark Palette
const Color kBg           = rgb(12, 14, 18);        // Main deep dark grey-blue background
const Color kHeaderBg     = rgb(18, 22, 28);        // Header background
const Color kCardBg       = rgb(20, 24, 30);        // Section container background
const Color kCardHeaderBg = rgb(26, 32, 40);        // Sub-header container background
const Color kCardEdge     = rgb(38, 46, 58);        // Card boundary outline
const Color kCardEdgeGlow = rgb(50, 60, 76);        // Hover/focus card outline
const Color kControlBg    = rgb(15, 18, 24);        // Inner control container
const Color kControlEdge  = rgb(42, 50, 64);        // Control stroke outline

const Color kText         = rgb(225, 232, 242);     // Crisp primary text
const Color kDim          = rgb(120, 132, 150);     // Secondary dimmed text
const Color kMuted        = rgb(75, 85, 102);       // Subtle grid/background text

const Color kKick         = rgb(255, 145, 30);      // Kick primary glow (Amber / Neon Orange)
const Color kKickDim      = rgb(140, 75, 15);       // Kick track fill
const Color kBass         = rgb(0, 220, 150);       // Bass primary glow (Bright Cyan-Green / Emerald)
const Color kBassDim      = rgb(0, 100, 70);        // Bass track fill

const Color kPitch        = rgb(60, 170, 255);      // Pitch overlay line (Electric Blue)
const Color kAnchor       = rgb(255, 215, 0);       // Anchor vertical lines (Gold / Bright Yellow)
const Color kGate         = rgb(255, 75, 85);       // Tail gate marker (Crimson / Coral Red)

constexpr int kScopeX = 14, kScopeY = 44, kScopeW = 872, kScopeH = 160;
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
    // Top bar header choices
    add(Kind::Choice, P_Key, 300, 8, 60, 24, "KEY");
    add(Kind::Choice, P_PlayMode, 410, 8, 96, 24, "MODE");
    add(Kind::Choice, P_PhaseMode, 560, 8, 80, 24, "PHASE");

    // Step row (Left card)
    add(Kind::Fixed, 0, 18, 224, 68, 38, "K");
    for (int i = 0; i < 3; ++i) add(Kind::Step, P_Step1On + i, 18 + (i + 1) * 74, 224, 68, 38, "B");
    add(Kind::Knob, P_TargetPhase, 18, 270, 62, 50, "TARGET");
    for (int i = 0; i < 3; ++i) add(Kind::Knob, P_Emph1 + i, 18 + (i + 1) * 74, 270, 62, 50, "EMPH");

    // Feel row (Right card bottom controls)
    const uint32_t feel[] = {P_Flow, P_Roll, P_Push, P_Gel, P_LockStyle, P_Balance, P_Volume};
    const char* fl[] = {"FLOW", "ROLL", "PUSH", "GEL", "LOCK", "BAL", "VOL"};
    for (int i = 0; i < 7; ++i) add(Kind::Knob, feel[i], 338 + i * 76, 320, 62, 50, fl[i]);

    // Kick parameters
    const struct { uint32_t id; const char* n; bool choice; } kick[] = {
        {P_KickTune, "TUNE", 0}, {P_Sweep, "SWEEP", 0}, {P_Curve, "CURVE", 0}, {P_Punch, "PUNCH", 0},
        {P_Settle, "SETTLE", 0}, {P_StartPhase, "START", 0}, {P_Length, "LENGTH", 0}, {P_Shape, "SHAPE", 0},
        {P_Fishtail, "FISH", 0}, {P_ClickType, "CLICK", 1}, {P_ClickLevel, "CLK LVL", 0}};
    for (int i = 0; i < 11; ++i)
        add(kick[i].choice ? Kind::Choice : Kind::Knob, kick[i].id, 70 + i * 62, 382, 62, 50, kick[i].n);

    // Kick Drive parameters
    const struct { uint32_t id; const char* n; bool choice; } drive[] = {
        {P_KickDrive, "DRIVE", 0}, {P_SatType, "TYPE", 1}, {P_SatBias, "BIAS", 0},
        {P_SatEnvFollow, "FOLLOW", 0}, {P_SatTone, "TONE", 0}};
    for (int i = 0; i < 5; ++i)
        add(drive[i].choice ? Kind::Choice : Kind::Knob, drive[i].id, 70 + i * 62, 438, 62, 50, drive[i].n);

    // Bass parameters
    const struct { uint32_t id; const char* n; bool choice; } bass[] = {
        {P_BassOct, "OCT", 1}, {P_Interval, "INTVL", 0}, {P_Wave, "WAVE", 0}, {P_Sub, "SUB", 0},
        {P_FilterType, "FILTER", 1}, {P_Cutoff, "CUTOFF", 0}, {P_Res, "RES", 0}, {P_EnvMod, "ENV MOD", 0},
        {P_FDecay, "F.DEC", 0}, {P_Attack, "ATTACK", 0}, {P_Snap, "SNAP", 0}, {P_Gate, "GATE", 0},
        {P_BassDrive, "DRIVE", 0}, {P_Third, "3RD-H", 0}, {P_Analog, "ANALOG", 0}};
    for (int i = 0; i < 15; ++i)
        add(bass[i].choice ? Kind::Choice : Kind::Knob, bass[i].id, 70 + i * 54, 494, 54, 50, bass[i].n);
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
    const float cx = c.x + c.w / 2.f, cy = c.y + 24.f, r = 12.f;

    // Label
    g.text(int(cx), c.y + 1, c.label, kDim, 1, 1);

    // Outer dial rim
    const float a0 = kPi * 0.75f, a1 = kPi * 2.25f;
    g.arc(cx, cy, r + 1.f, a0, a1, rgb(25, 30, 38), 3.f);
    g.arc(cx, cy, r, a0, a1, kCardEdge, 1.5f);

    // Bipolar vs Unipolar arc rendering
    const bool bip = p->min < 0 && p->max > 0 && std::fabs(p->min + p->max) < 1e-9;
    const float an = a0 + float(n) * (a1 - a0);
    const Color mainCol = c.id >= P_BassOct ? kBass : (c.id >= P_KickTune ? kKick : kPitch);

    if (bip) {
        float aMid = (a0 + a1) * 0.5f;
        g.arc(cx, cy, r, std::min(an, aMid), std::max(an, aMid), mainCol, 2.5f);
    } else {
        g.arc(cx, cy, r, a0, an, mainCol, 2.5f);
    }

    // Pointer needle
    float needleR = r - 2.f;
    g.thickLine(cx, cy, cx + std::cos(an) * needleR, cy + std::sin(an) * needleR, kText, 1.5f);
    g.fillCircle(cx, cy, 3.f, kControlBg);
    g.circle(cx, cy, 3.f, mainCol, 1.f);

    // Value text
    g.text(int(cx), c.y + 40, paramText(c.id, v), kText, 1, 1);
}

void Panel::drawChoice(Graphics& g, const Ctl& c) {
    const bool header = c.y < 40;
    const int bx = c.x + (header ? 0 : 2), bw = c.w - (header ? 0 : 4);
    const int by = header ? c.y : c.y + 16, bh = header ? c.h : 18;
    if (header) g.text(c.x - 6, c.y + 8, c.label, kDim, 1, 2);
    else g.text(c.x + c.w / 2, c.y + 1, c.label, kDim, 1, 1);

    // Futuristic pill container
    g.fillRoundRect(bx, by, bw, bh, 4, kControlBg);
    g.roundRect(bx, by, bw, bh, 4, kControlEdge);
    g.text(bx + bw / 2, by + (bh - 7) / 2, paramText(c.id, host_.get(c.id)), kText, 1, 1);
}

void Panel::drawStep(Graphics& g, const Ctl& c, int idx) {
    const bool on = c.kind == Kind::Fixed || host_.get(c.id) > 0.5;
    const Color col = c.kind == Kind::Fixed ? kKick : kBass;

    // Modern button container
    g.fillRoundRect(c.x, c.y, c.w, c.h, 6, on ? rgb((col >> 16 & 255) / 5, (col >> 8 & 255) / 5, (col & 255) / 5) : kControlBg);
    g.roundRect(c.x, c.y, c.w, c.h, 6, on ? col : kControlEdge);

    // Playhead indicator top bar
    if (host_.playing()) {
        const double ph = host_.beatPos() - std::floor(host_.beatPos());
        if (int(ph * 4) == idx) {
            g.fillRoundRect(c.x + 4, c.y + 3, c.w - 8, 3, 1, rgb(255, 255, 255));
        }
    }
    std::string s = c.kind == Kind::Fixed ? "KICK" : std::string("BASS ") + char('0' + idx);
    g.text(c.x + c.w / 2, c.y + (c.h - 7) / 2 + 1, s, on ? kText : kDim, 1, 1);
}

void Panel::drawScope(Graphics& g) {
    const int x0 = kScopeX, y0 = kScopeY, w = kScopeW, h = kScopeH;

    // Container background with rounded corners and card border
    g.fillRoundRect(x0, y0, w, h, 6, kCardBg);
    g.roundRect(x0, y0, w, h, 6, kCardEdge);

    const int py = y0 + 16, ph = h - 20, cy = py + ph / 2;
    const BeatPlan& pl = preview_.plan;
    const double Tb = pl.Tbeat;
    auto tx = [&](double t) { return x0 + float(t / Tb * w); };

    // Lanes grid and headers
    const char* names[4] = {"STEP 1 [KICK]", "STEP 2 [BASS 1]", "STEP 3 [BASS 2]", "STEP 4 [BASS 3]"};
    for (int i = 0; i < 4; ++i) {
        if (i) g.line(x0 + i * w / 4, y0, x0 + i * w / 4, y0 + h, kCardEdge, 0.7f);
        const bool on = i == 0 || pl.notes[i - 1].active;
        g.text(x0 + i * w / 4 + 8, y0 + 5, names[i], on ? (i ? kBass : kKick) : kMuted);
    }
    // Sub-beat grid divisions (1/32)
    for (int i = 0; i < 8; ++i) {
        if (i % 2 != 0) g.line(x0 + i * w / 8, y0 + 16, x0 + i * w / 8, y0 + h, rgb(28, 34, 44), 0.5f);
    }
    g.line(x0, cy, x0 + w, cy, kCardEdge, 0.6f);

    const float sc = (ph / 2.f - 2.f) / scopePeak_;
    for (int c = 0; c < w; ++c) {
        // Translucent fill for bass & kick waveforms
        if (std::fabs(bMax_[c] - bMin_[c]) > 0.001f)
            g.thickLine(x0 + c + 0.5f, cy - bMax_[c] * sc, x0 + c + 0.5f, cy - bMin_[c] * sc, kBass, 1.f, 0.75f);
        if (std::fabs(kMax_[c] - kMin_[c]) > 0.001f)
            g.thickLine(x0 + c + 0.5f, cy - kMax_[c] * sc, x0 + c + 0.5f, cy - kMin_[c] * sc, kKick, 0.75f);
    }

    // Pitch curve (log axis 30 Hz .. 1.5 kHz)
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
        if (px > 0) g.thickLine(px, pyy, x, y, kPitch, 1.5f, 0.85f);
        px = x; pyy = y;
    }
    for (double f : {50.0, 100.0, 200.0, 500.0, 1000.0}) {
        g.fillRect(x0 + w - 4, int(fy(f)), 4, 1, kPitch);
        g.text(x0 + w - 7, int(fy(f)) - 3, fmt("%.0f", f), kPitch, 1, 2);
    }
    const double settle = tau + pl.kick.model.settleTime();
    g.fillCircle(tx(settle), fy(pl.f0), 3.5f, kPitch);
    g.circle(tx(settle), fy(pl.f0), 3.5f, rgb(255, 255, 255), 1.f);

    // Anchors
    for (int i = 0; i < 3; ++i) {
        const auto& n = pl.notes[i];
        if (!n.active) continue;
        const float x = tx(n.onset);
        for (int y = py; y < py + ph; y += 4) g.fillRect(int(x), y, 1, 2, kAnchor, 0.9f);
        g.fillRoundRect(int(x) + 3, y0 + h - 24, 52, 10, 2, kControlBg, 0.8f);
        g.text(int(x) + 5, y0 + h - 22, fmt("%+.1f DB", pl.flowGainDb[i]), kBass);
        if (i == pl.firstActive) {
            const double ta = n.onset - tau;
            const double resid = wrapHalf(pl.kick.model.phase(ta) - pl.notes[i].phase0) * 360.0;
            g.fillRoundRect(int(x) + 3, y0 + h - 13, 76, 10, 2, kControlBg, 0.8f);
            g.text(int(x) + 5, y0 + h - 11, fmt("ERR %.1f DEG", std::fabs(resid) < 0.05 ? 0.0 : resid), kAnchor);
        }
    }
    // Tail gate marker
    if (!pl.followMode && pl.firstActive >= 0) {
        const float x = tx(tau + pl.kick.amp.gateEnd);
        g.thickLine(x, py + ph - 10, x - 5, py + ph, kGate, 1.5f);
        g.thickLine(x, py + ph - 10, x + 5, py + ph, kGate, 1.5f);
        g.thickLine(x - 5, py + ph, x + 5, py + ph, kGate, 1.5f);
    }
    // Animated Playhead
    if (host_.playing()) {
        const double b = host_.beatPos();
        const int pxHead = x0 + int((b - std::floor(b)) * w);
        g.fillRect(pxHead, y0, 2, h, rgb(255, 255, 255), 0.85f);
    }
}

void Panel::drawHandoff(Graphics& g) {
    const int x0 = 338, y0 = 224, w = 332, h = 88;
    g.fillRoundRect(x0, y0, w, h, 6, kCardBg);
    g.roundRect(x0, y0, w, h, 6, kCardEdge);

    // Header tag
    g.fillRoundRect(x0 + 6, y0 + 4, 172, 12, 2, kCardHeaderBg);
    g.text(x0 + 10, y0 + 7, "HANDOFF +-3 MS AT FIRST ANCHOR", kDim);

    const BeatPlan& pl = preview_.plan;
    const int fs = 48000;
    if (pl.firstActive < 0) { g.text(x0 + w / 2, y0 + h / 2, "NO BASS STEP", kDim, 1, 1); return; }
    const double ta = pl.notes[pl.firstActive].onset;
    const long c = long(ta * fs), half = long(0.003 * fs);
    const int cy = y0 + 16 + (h - 20) / 2;

    // Anchor center line
    g.line(x0 + w / 2, y0 + 16, x0 + w / 2, y0 + h, kAnchor, 0.7f);
    g.line(x0, cy, x0 + w, cy, kCardEdge, 0.5f);

    float peak = 0.05f;
    for (long i = c - half; i <= c + half; ++i)
        if (i >= 0 && i < long(preview_.kick.size())) peak = std::max({peak, std::fabs(preview_.kick[i]), std::fabs(preview_.bass[i])});
    const float sc = (h - 24) / 2.f / peak;
    for (int pass = 0; pass < 2; ++pass) {
        const auto& v = pass ? preview_.kick : preview_.bass;
        float px = 0, pyy = 0;
        for (long i = c - half; i <= c + half; ++i) {
            if (i < 0 || i >= long(v.size())) continue;
            const float x = x0 + float(i - (c - half)) / (2 * half) * w, y = cy - v[i] * sc;
            if (i > c - half) g.thickLine(px, pyy, x, y, pass ? kKick : kBass, 1.5f, 0.9f);
            px = x; pyy = y;
        }
    }
}

void Panel::render(Graphics& g) {
    refreshPreview();
    g.clear(kBg);

    // Header Bar
    g.fillGradientV(0, 0, W, 36, kHeaderBg, kBg);
    g.line(0, 36, W, 36, kCardEdge, 0.8f);
    g.text(14, 8, "KIKSET", kKick, 3);
    g.text(132, 14, "COMBO KICK + BASS ENGINE", kDim);

    for (size_t i = 0; i < ctl_.size(); ++i) {
        const Ctl& c = ctl_[i];
        if (c.y < 40) drawChoice(g, c); // Draw header controls top level
    }

    // Wide Oscilloscope Display
    drawScope(g);

    // Section 1: Pattern & Step Sequencer (Left Card)
    g.fillRoundRect(14, 218, 312, 108, 6, kCardBg);
    g.roundRect(14, 218, 312, 108, 6, kCardEdge);

    // Section 2: Handoff Zoom (Middle Card)
    drawHandoff(g);

    // Section 3: Analytic Solver Display (Right Card)
    g.fillRoundRect(680, 224, 206, 88, 6, kCardBg);
    g.roundRect(680, 224, 206, 88, 6, kCardEdge);
    const BeatPlan& pl = preview_.plan;
    const auto& s = pl.solve;
    g.fillRoundRect(686, 228, 54, 12, 2, kCardHeaderBg);
    g.text(690, 231, "SOLVER", kAnchor);
    g.text(690, 244, fmt("NUDGE %.2f MS", s.nudgeMs), kText);
    g.text(690, 255, fmt("D PITCH %.0f C", s.dPitchCents), kText);
    g.text(690, 266, fmt("D PHASE %.0f DEG", s.dPhaseDeg), kText);
    g.text(690, 277, fmt("F0 %.1f HZ", pl.f0), kText);
    g.fillRoundRect(800, 244, 78, 14, 3, s.converged ? rgb(0, 60, 40) : rgb(80, 20, 25));
    g.roundRect(800, 244, 78, 14, 3, s.converged ? kBass : kGate);
    g.text(839, 248, s.converged ? "LOCKED" : "UNLOCKED", s.converged ? kBass : kGate, 1, 1);
    g.text(800, 266, fmt("RESID %.0E CYC", std::fabs(s.residual)), kDim);

    // Section 4: Feel & Flow Row
    g.fillRoundRect(332, 316, 554, 58, 6, kCardBg);
    g.roundRect(332, 316, 554, 58, 6, kCardEdge);

    // Section 5: Kick Synthesizer Panel
    g.fillRoundRect(14, 378, 872, 55, 6, kCardBg);
    g.roundRect(14, 378, 872, 55, 6, kCardEdge);
    g.fillRoundRect(18, 382, 46, 47, 4, kCardHeaderBg);
    g.text(41, 402, "KICK", kKick, 1, 1);

    // Section 6: Kick Drive Panel
    g.fillRoundRect(14, 434, 872, 55, 6, kCardBg);
    g.roundRect(14, 434, 872, 55, 6, kCardEdge);
    g.fillRoundRect(18, 438, 46, 47, 4, kCardHeaderBg);
    g.text(41, 458, "DRIVE", kKick, 1, 1);

    // Section 7: Bass Synthesizer Panel
    g.fillRoundRect(14, 490, 872, 55, 6, kCardBg);
    g.roundRect(14, 490, 872, 55, 6, kCardEdge);
    g.fillRoundRect(18, 494, 46, 47, 4, kCardHeaderBg);
    g.text(41, 514, "BASS", kBass, 1, 1);

    // Render controls
    for (size_t i = 0; i < ctl_.size(); ++i) {
        const Ctl& c = ctl_[i];
        if (c.y < 40) continue; // Already drawn header controls
        switch (c.kind) {
            case Kind::Knob: drawKnob(g, c); break;
            case Kind::Choice: drawChoice(g, c); break;
            case Kind::Step: drawStep(g, c, c.id - P_Step1On + 1); break;
            case Kind::Fixed: drawStep(g, c, 0); break;
        }
    }
}

}  // namespace kikset::gui
