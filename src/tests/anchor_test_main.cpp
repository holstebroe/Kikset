// Anchor exactness (design §12): plan-level over the full key/tempo/lock matrix,
// plus an audio-domain phase measurement on a sample of it.
#include <chrono>

#include "TestUtil.hpp"

using namespace kikset;

int main() {
    const auto t0 = std::chrono::steady_clock::now();
    SolverCache cache;
    long cases = 0, maxIter = 0;
    double worst = 0.0;
    for (int key = 0; key < 12; ++key)
        for (int bpm = 130; bpm <= 180; ++bpm)
            for (double lock : {0.0, 0.5, 1.0})
                for (int follow = 0; follow < 2; ++follow) {
                    ParamSet p;
                    p.set(P_LockStyle, lock);
                    p.set(P_PhaseMode, follow);
                    BeatPlan plan;
                    buildBeatPlan(plan, p, bpm, key, cache);
                    ++cases;
                    const auto& s = plan.solve;
                    CHECK(s.converged, "key %d bpm %d lock %.1f follow %d resid %g", key, bpm, lock, follow, s.residual);
                    worst = std::max(worst, std::fabs(s.residual));
                    maxIter = std::max<long>(maxIter, s.iterations);
                    CHECK(s.shape.tau >= 0.0 && s.shape.tau <= 0.006 + 1e-12, "tau %g", s.shape.tau);
                    CHECK(s.shape.O >= 0.0 && s.shape.O <= 6.5 + 1e-9, "O %g", s.shape.O);
                    // kick phase at the first bass anchor equals the target phase
                    const double ta = plan.T16 + 0.0;
                    const double ph = plan.kick.model.phase(ta - plan.kick.shape.tau);
                    CHECK(std::fabs(wrapHalf(ph - 0.0)) < 1e-6, "phase %g", ph);
                    // Follow mode, unison: every note starts on the kick's own phase
                    if (follow) {
                        for (int i = 0; i < 3; ++i) {
                            const double pk = plan.kick.model.phase(plan.notes[i].onset - plan.kick.shape.tau);
                            CHECK(std::fabs(wrapHalf(pk - plan.notes[i].phase0)) < 1e-6, "follow note %d", i);
                        }
                    }
                }
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("plan matrix: %ld cases, worst residual %.2e cycles, max Newton iterations %ld (%.1fs)\n", cases, worst,
                maxIter, sec);

    // Audio-domain check: measured kick phase at the first anchor.
    double worstDeg = 0.0;
    for (int key : {0, 5, 7, 9, 11})
        for (int bpm : {138, 145, 150, 170})
            for (double psiDeg : {0.0, 90.0}) {
                ParamSet p;
                p.set(P_Gel, 0.5);             // keep the tail alive across the measurement window
                p.set(P_Length, 2.0);
                p.set(P_Settle, 0.4);
                p.set(P_ClickLevel, -60);      // click is HP'd but keep the test clean
                p.set(P_TargetPhase, psiDeg);
                const double fs = 48000.0;
                BeatPreview bp = renderBeatPreview(p, bpm, key, fs);
                const double f0 = bp.plan.f0;
                const double ta = bp.plan.T16 + 0.0;
                // renderBeatPreview renders with the plan's kick onset offset (tau) built in.
                const double ph = fitPhase(bp.kick, fs, f0, ta, 2.0 / f0);
                const double err = std::fabs(wrapHalf(ph - psiDeg / 360.0)) * 360.0;
                worstDeg = std::max(worstDeg, err);
                CHECK(err < 2.0, "key %d bpm %d psi %.0f measured err %.2f deg", key, bpm, psiDeg, err);
            }
    std::printf("audio-domain anchor phase: worst error %.3f deg\n", worstDeg);
    return finish("anchor_test");
}
