// CLI: kikset_render --bpm 145 --key 5 --bars 4 --out file.wav [--stems] [--param id=val ...]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../core/KiksetEngine.hpp"
#include "WavWriter.hpp"

using namespace kikset;

int main(int argc, char** argv) {
    double bpm = 145.0, fs = 48000.0;
    int key = 5, bars = 4;
    std::string out = "kikset.wav";
    bool stems = false;
    KiksetEngine e;
    for (int i = 1; i < argc; ++i) {
        auto is = [&](const char* s) { return !std::strcmp(argv[i], s); };
        if (is("--bpm") && i + 1 < argc) bpm = std::atof(argv[++i]);
        else if (is("--key") && i + 1 < argc) key = std::atoi(argv[++i]);
        else if (is("--bars") && i + 1 < argc) bars = std::atoi(argv[++i]);
        else if (is("--sr") && i + 1 < argc) fs = std::atof(argv[++i]);
        else if (is("--out") && i + 1 < argc) out = argv[++i];
        else if (is("--stems")) stems = true;
        else if (is("--param") && i + 1 < argc) {
            unsigned id = 0; double v = 0;
            if (std::sscanf(argv[++i], "%u=%lf", &id, &v) == 2) e.setParam(id, v);
            else { std::fprintf(stderr, "bad --param\n"); return 1; }
        } else { std::fprintf(stderr, "unknown arg %s\n", argv[i]); return 1; }
    }
    e.setSampleRate(fs);
    e.setParam(P_Key, key);
    const size_t n = size_t(fs * 240.0 / bpm * bars);
    std::vector<float> l(n), r(n), k(n), b(n);
    Transport tr{true, 0.0, bpm};
    const uint32_t block = 512;
    for (size_t pos = 0; pos < n; pos += block) {
        const uint32_t m = uint32_t(std::min<size_t>(block, n - pos));
        e.process(&l[pos], &r[pos], m, tr, &k[pos], &b[pos]);
        tr.beatPos += m * bpm / 60.0 / fs;
    }
    if (!writeWav16(out, l, fs)) { std::fprintf(stderr, "cannot write %s\n", out.c_str()); return 1; }
    if (stems) {
        writeWav16(out + ".kick.wav", k, fs);
        writeWav16(out + ".bass.wav", b, fs);
    }
    const BeatPlan& p = e.lastPlan();
    std::printf("rendered %d bars @ %.1f BPM, f0 %.2f Hz, solver: nudge %.2f ms, dStartPitch %.0f c, dPhase %.1f deg, resid %.2e\n",
                bars, bpm, p.f0, p.solve.nudgeMs, p.solve.dPitchCents, p.solve.dPhaseDeg, p.solve.residual);
    return 0;
}
