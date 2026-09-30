#include "TestUtil.hpp"

using namespace kikset;

static double meanWindow(const std::vector<float>& x, double fs, double t0, double len) {
    double acc = 0, wsum = 0;
    const long i0 = long(t0 * fs), n = long(len * fs);
    for (long i = 0; i < n && i0 + i < long(x.size()); ++i) {
        const double w = 0.5 - 0.5 * std::cos(2 * kPi * (i + 0.5) / n);
        acc += w * x[i0 + i];
        wsum += w;
    }
    return acc / wsum;
}

int main() {
    const double fs = 48000.0;

    // --- oversampler group delay
    {
        Oversampler4 os;
        const double f = 50.0;
        std::vector<float> y;
        for (int i = 0; i < 4800; ++i) y.push_back(float(os.process(std::sin(2 * kPi * f * i / fs), [](double v) { return v; })));
        // y[n] should equal sin(2 pi f (n - D)/fs)
        const double ph = fitPhase(y, fs, f, 2000.0 / fs, 0.04);  // phase of y at t0 relative to sin(2 pi f (t - t0))
        const double expect = f * (2000.0 / fs - Oversampler4::kDelay / fs);
        const double d = wrapHalf(ph - expect);
        CHECK(std::fabs(d) * 360.0 < 0.05, "oversampler delay mismatch %.4f deg (measured D = %.3f samples)", d * 360.0,
              Oversampler4::kDelay - d / f * fs);
    }

    // --- saturation phase safety + DC
    {
        double worstDeg = 0, worstDc = 0;
        for (int type = 0; type < 4; ++type)
            for (double bias : {-1.0, 1.0})
                for (double tone : {-1.0, 1.0})
                    for (double ef : {0.0, 0.3, 1.0}) {
                        ParamSet base;
                        base.set(P_Gel, 0.5);
                        base.set(P_Length, 2.0);
                        base.set(P_Settle, 0.4);  // fully settled before the measurement window
                        base.set(P_ClickLevel, -60);
                        base.set(P_SatType, type);
                        base.set(P_SatBias, bias);
                        base.set(P_SatTone, tone);
                        base.set(P_SatEnvFollow, ef);
                        base.set(P_KickDrive, 0.0);
                        ParamSet hot = base;
                        hot.set(P_KickDrive, 1.0);
                        auto a = renderBeatPreview(base, 145, 5, fs);
                        auto b = renderBeatPreview(hot, 145, 5, fs);
                        const double f0 = a.plan.f0, ta = a.plan.T16;
                        const double pa = fitPhase(a.kick, fs, f0, ta, 2.0 / f0);
                        const double pb = fitPhase(b.kick, fs, f0, ta, 2.0 / f0);
                        const double dg = std::fabs(wrapHalf(pb - pa)) * 360.0;
                        worstDeg = std::max(worstDeg, dg);
                        // ef = 1 with bias lets the shaper input amplitude sweep from saturated to linear inside
                        // the fit window; the least-squares model cannot follow that, so allow more slack there.
                        CHECK(dg < (ef > 0.5 ? 5.0 : 1.0), "type %d bias %g tone %g ef %g: %.3f deg", type, bias, tone, ef, dg);
                        // DC: Hann-weighted mean over 3 cycles in the tail vs. fundamental amplitude
                        double amp = 0;
                        fitPhase(b.kick, fs, f0, ta, 2.0 / f0, &amp);
                        const double dc = std::fabs(meanWindow(b.kick, fs, ta - 1.5 / f0, 3.0 / f0));
                        worstDc = std::max(worstDc, dc / std::max(amp, 1e-9));
                        CHECK(dc / std::max(amp, 1e-9) < 0.04, "type %d bias %g tone %g ef %g: dc/amp %.4f", type, bias, tone, ef, dc / amp);
                    }
        std::printf("saturation: worst fundamental phase shift %.3f deg, worst DC/fund %.4f\n", worstDeg, worstDc);
    }

    // --- determinism at an integer samples-per-beat tempo
    {
        KiksetEngine e;
        e.setSampleRate(fs);
        const double bpm = 120.0;
        const uint32_t beatLen = 24000;
        std::vector<float> l(beatLen * 104), r(l.size());
        Transport tr{true, 0.0, bpm};
        for (size_t pos = 0; pos < l.size(); pos += 480) {
            e.process(&l[pos], &r[pos], 480, tr);
            tr.beatPos += 480 * bpm / 60.0 / fs;
        }
        double maxd = 0, peak = 0;
        for (uint32_t i = 0; i < beatLen; ++i) {
            maxd = std::max(maxd, double(std::fabs(l[beatLen * 2 + i] - l[beatLen * 102 + i])));
            peak = std::max(peak, double(std::fabs(l[beatLen * 2 + i])));
        }
        CHECK(peak > 0.1, "beat is silent (peak %g)", peak);
        // a note onset can land one sample earlier/later when the host beat position carries
        // ~1e-10 beats of rounding, which shows up as a single first-sample difference (< -74 dBFS)
        CHECK(maxd < 2e-4, "beat 2 vs 102 differ by %g", maxd);
        std::printf("determinism: peak %.3f, max diff %.2e\n", peak, maxd);
    }

    // --- Reset tail gate
    {
        ParamSet p;
        p.set(P_Length, 2.5);
        p.set(P_ClickLevel, -60);
        auto b = renderBeatPreview(p, 145, 5, fs);
        const double T16 = b.plan.T16;
        const double end = T16 + p[P_Gel] * T16 + 0.002 + 0.001;  // gate end + fade + oscillator ring margin
        double mx = 0;
        for (size_t i = size_t(end * fs); i < b.kick.size(); ++i) mx = std::max(mx, double(std::fabs(b.kick[i])));
        CHECK(mx < 1e-5, "kick leaks past tail gate: %g", mx);
        // Follow keeps the long tail
        p.set(P_PhaseMode, 1);
        auto f = renderBeatPreview(p, 145, 5, fs);
        double mf = 0;
        for (size_t i = size_t(end * fs); i < size_t((end + 0.05) * fs); ++i) mf = std::max(mf, double(std::fabs(f.kick[i])));
        CHECK(mf > 1e-3, "follow mode should keep the tail (%g)", mf);
    }

    // --- Flow
    {
        ParamSet p;
        BeatPlan pl;
        SolverCache c;
        buildBeatPlan(pl, p, 145, 5, c);
        CHECK(pl.flowGainDb[0] < -1.5 && pl.flowGainDb[0] > -5.0, "first-after-kick gain %.2f dB", pl.flowGainDb[0]);
        CHECK(pl.flowGainDb[1] > pl.flowGainDb[0], "step 2 should be louder than step 1");
        const double g2on = pl.flowGainDb[1];
        p.set(P_Step1On, 0);
        buildBeatPlan(pl, p, 145, 5, c);
        CHECK(pl.firstActive == 1, "first active should move to step 2");
        CHECK(pl.flowGainDb[1] < g2on - 1.0, "step 2 is now first after kick (%.2f vs %.2f dB)", pl.flowGainDb[1], g2on);
        p.set(P_Flow, 0.0);
        buildBeatPlan(pl, p, 145, 5, c);
        CHECK(std::fabs(pl.flowGainDb[1]) < 1e-9, "flow 0 = unity");
        p.set(P_Emph2, 1.0);
        buildBeatPlan(pl, p, 145, 5, c);
        CHECK(std::fabs(pl.flowGainDb[1] - 6.02) < 0.05, "emphasis +1 = +6 dB (%.2f)", pl.flowGainDb[1]);
    }

    // --- Scope data: stems add up to the mix
    {
        auto b = renderBeatPreview(ParamSet{}, 145, 5, fs);
        double md = 0, pk = 0;
        for (size_t i = 0; i < b.mix.size(); ++i) {
            md = std::max(md, double(std::fabs(b.mix[i] - b.kick[i] - b.bass[i])));
            pk = std::max(pk, double(std::fabs(b.bass[i])));
        }
        CHECK(md < 1e-5, "stems differ from mix by %g", md);
        CHECK(pk > 0.05, "bass is silent");
    }

    // --- Transport: jump resync and MIDI gate on a stopped transport
    {
        KiksetEngine e;
        e.setSampleRate(fs);
        const double bpm = 145.0;
        std::vector<float> l(480), r(480);
        Transport tr{true, 0.0, bpm};
        double peakBefore = 0;
        for (int blk = 0; blk < 100; ++blk) {
            e.process(l.data(), r.data(), 480, tr);
            tr.beatPos += 480 * bpm / 60.0 / fs;
            for (float v : l) peakBefore = std::max(peakBefore, double(std::fabs(v)));
        }
        tr.beatPos = 40.37;  // jump mid-beat
        double mid = 0, later = 0;
        bool finite = true;
        for (int blk = 0; blk < 200; ++blk) {
            e.process(l.data(), r.data(), 480, tr);
            tr.beatPos += 480 * bpm / 60.0 / fs;
            for (float v : l) finite = finite && std::isfinite(v);
            // next beat at 41.0: 0.63 beat = 0.26 s = block 26
            double pk = 0;
            for (float v : l) pk = std::max(pk, double(std::fabs(v)));
            if (blk >= 2 && blk < 24) mid = std::max(mid, pk);
            if (blk >= 26 && blk < 40) later = std::max(later, pk);
        }
        CHECK(finite, "non-finite output after jump");
        CHECK(mid < 1e-4, "no mid-beat start after a jump (peak %g)", mid);
        CHECK(later > 0.05, "kick resumes on the next beat (peak %g)", later);

        KiksetEngine g;
        g.setSampleRate(fs);
        g.setParam(P_PlayMode, 1);
        Transport stopped{false, 0.0, bpm};
        double silent = 0, playing = 0, released = 0;
        for (int blk = 0; blk < 10; ++blk) { g.process(l.data(), r.data(), 480, stopped); for (float v : l) silent = std::max(silent, double(std::fabs(v))); }
        g.noteOn(60);
        for (int blk = 0; blk < 60; ++blk) { g.process(l.data(), r.data(), 480, stopped); for (float v : l) playing = std::max(playing, double(std::fabs(v))); }
        g.noteOff(60);
        for (int blk = 0; blk < 4; ++blk) g.process(l.data(), r.data(), 480, stopped);
        for (int blk = 0; blk < 10; ++blk) { g.process(l.data(), r.data(), 480, stopped); for (float v : l) released = std::max(released, double(std::fabs(v))); }
        CHECK(silent == 0.0, "gate mode idle must be silent");
        CHECK(playing > 0.1, "gate mode plays on note-on with stopped transport (%g)", playing);
        CHECK(released < 1e-5, "gate mode silent after note-off (%g)", released);
        CHECK(g.lastPlan().root == 0, "MIDI note 60 sets root C (got %d)", g.lastPlan().root);
    }

    return finish("dsp_test");
}
