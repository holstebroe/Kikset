#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

inline bool writeWav16(const std::string& path, const std::vector<float>& x, double fs) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint32_t n = uint32_t(x.size()), bytes = n * 2, sr = uint32_t(fs);
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); w32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    w32(16); w16(1); w16(1); w32(sr); w32(sr * 2); w16(2); w16(16);
    std::fwrite("data", 1, 4, f); w32(bytes);
    for (float v : x) {
        const int16_t s = int16_t(std::lround(std::clamp(v, -1.f, 1.f) * 32767.f));
        std::fwrite(&s, 2, 1, f);
    }
    std::fclose(f);
    return true;
}
