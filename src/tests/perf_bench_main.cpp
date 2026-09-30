#include <chrono>

#include "TestUtil.hpp"

using namespace kikset;

int main() {
    const double fs = 48000.0;
    KiksetEngine e;
    e.setSampleRate(fs);
    const double secs = 20.0;
    const size_t total = size_t(fs * secs);
    std::vector<float> l(512), r(512);
    Transport tr{true, 0.0, 145.0};
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t pos = 0; pos < total; pos += 512) {
        e.process(l.data(), r.data(), 512, tr);
        tr.beatPos += 512 * 145.0 / 60.0 / fs;
    }
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double pct = 100.0 * wall / secs;
    std::printf("perf: %.2f%% of one core at 48 kHz (20 s of audio in %.3f s)\n", pct, wall);
    // Design target is < 1 %; allow headroom for shared CI runners.
    CHECK(pct < 5.0, "too slow: %.2f%%", pct);
    return finish("perf_bench");
}
