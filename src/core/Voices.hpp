// Kick and bass voices (design §5). Both are pure functions of time since
// their onset (closed-form phase and envelopes), so renders are deterministic
// and onsets are sub-sample exact.
#pragma once
#include <array>

#include "BeatPlan.hpp"
#include "ClickBank.hpp"
#include "Filters.hpp"
#include "Saturator.hpp"

namespace kikset {

class KickVoice {
public:
    // onsetAbs: absolute sample position (fractional) of the kick trigger incl. nudge.
    void start(const KickPlan& plan, double onsetAbs, double fs, const ClickBank* clicks);
    double render(double abs);
    void kill(double abs);   // 3 ms fade out
    void cancelPending();    // drop if not yet started
    bool active() const { return active_; }

private:
    double generator(double t) const;  // body input at kick time t (>= 0 else 0)
    double pipeline(double t);
    KickPlan plan_;
    double onset_ = 0.0, fs_ = 48000.0;
    const ClickBank* clicks_ = nullptr;
    bool active_ = false, primed_ = false;
    double killAt_ = -1.0;
    Oversampler4 os_;
    EmphasisPair emph_;
    double tapeLp_ = 0.0, tapeG_ = 0.1;
    int dcTick_ = 0;
    double dcHold_ = 0.0;
};

class BassVoice {
public:
    void init(double fs) { fs_ = fs; }
    void schedule(const NotePlan& n, double onsetAbs);
    double render(double abs);
    void kill(double abs);
    void cancelPending() { qHead_ = qTail_ = 0; }
    bool active() const { return active_; }

private:
    void startNote(const NotePlan& n, double onsetAbs);
    static constexpr int kQ = 8;
    struct Item { NotePlan n; double onset; };
    std::array<Item, kQ> q_{};
    int qHead_ = 0, qTail_ = 0;
    NotePlan cur_;
    double onset_ = 0.0, fs_ = 48000.0, killAt_ = -1.0;
    bool active_ = false;
    Ladder24 ladder_;
    SvfLp12 svf_;
    Peaking eq_;
};

}  // namespace kikset
