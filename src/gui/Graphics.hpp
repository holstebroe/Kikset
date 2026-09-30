// Tiny software renderer: 0x00RRGGBB framebuffer, AA lines/arcs, 5x7 bitmap font.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace kikset::gui {

using Color = uint32_t;
constexpr Color rgb(int r, int g, int b) { return Color((r << 16) | (g << 8) | b); }

class Graphics {
public:
    Graphics(int w, int h) : w_(w), h_(h), px_(size_t(w) * h, 0) {}
    int width() const { return w_; }
    int height() const { return h_; }
    const uint32_t* data() const { return px_.data(); }

    void clear(Color c) { std::fill(px_.begin(), px_.end(), c); }
    void blend(int x, int y, Color c, float a);  // a in 0..1
    void fillRect(int x, int y, int w, int h, Color c, float a = 1.f);
    void rect(int x, int y, int w, int h, Color c, float a = 1.f);
    void line(float x0, float y0, float x1, float y1, Color c, float a = 1.f);  // Wu, 1 px
    void arc(float cx, float cy, float r, float a0, float a1, Color c, float thick = 2.f);
    void fillCircle(float cx, float cy, float r, Color c, float a = 1.f);
    // Uppercase-only 5x7 font. align: 0 left, 1 centre, 2 right.
    void text(int x, int y, const std::string& s, Color c, int scale = 1, int align = 0);
    static int textWidth(const std::string& s, int scale = 1) { return int(s.size()) * 6 * scale - scale; }
    bool writePpm(const char* path) const;

private:
    int w_, h_;
    std::vector<uint32_t> px_;
};

}  // namespace kikset::gui
