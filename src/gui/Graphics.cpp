#include "Graphics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace kikset::gui {

namespace {
// Classic 5x7 font, ASCII 0x20..0x5F, one byte per column (bit 0 = top row).
const uint8_t kFont[64][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5F, 0x00, 0x00}, {0x00, 0x07, 0x00, 0x07, 0x00},
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, {0x24, 0x2A, 0x7F, 0x2A, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62},
    {0x36, 0x49, 0x56, 0x20, 0x50}, {0x00, 0x05, 0x03, 0x00, 0x00}, {0x00, 0x1C, 0x22, 0x41, 0x00},
    {0x00, 0x41, 0x22, 0x1C, 0x00}, {0x2A, 0x1C, 0x7F, 0x1C, 0x2A}, {0x08, 0x08, 0x3E, 0x08, 0x08},
    {0x00, 0x50, 0x30, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08}, {0x00, 0x60, 0x60, 0x00, 0x00},
    {0x20, 0x10, 0x08, 0x04, 0x02}, {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31}, {0x18, 0x14, 0x12, 0x7F, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E}, {0x00, 0x36, 0x36, 0x00, 0x00},
    {0x00, 0x56, 0x36, 0x00, 0x00}, {0x08, 0x14, 0x22, 0x41, 0x00}, {0x14, 0x14, 0x14, 0x14, 0x14},
    {0x00, 0x41, 0x22, 0x14, 0x08}, {0x02, 0x01, 0x51, 0x09, 0x06}, {0x32, 0x49, 0x79, 0x41, 0x3E},
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01},
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F},
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}, {0x00, 0x7F, 0x41, 0x41, 0x00},
    {0x02, 0x04, 0x08, 0x10, 0x20}, {0x00, 0x41, 0x41, 0x7F, 0x00}, {0x04, 0x02, 0x01, 0x02, 0x04},
    {0x40, 0x40, 0x40, 0x40, 0x40}};

inline Color lerpColor(Color c0, Color c1, float t) {
    t = std::clamp(t, 0.f, 1.f);
    int r0 = (c0 >> 16) & 255, g0 = (c0 >> 8) & 255, b0 = c0 & 255;
    int r1 = (c1 >> 16) & 255, g1 = (c1 >> 8) & 255, b1 = c1 & 255;
    int r = int(r0 + (r1 - r0) * t + 0.5f);
    int g = int(g0 + (g1 - g0) * t + 0.5f);
    int b = int(b0 + (b1 - b0) * t + 0.5f);
    return rgb(r, g, b);
}
}  // namespace

void Graphics::blendFast(int x, int y, Color c, int alpha256) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_ || alpha256 <= 0) return;
    uint32_t& dst = px_[size_t(y) * w_ + x];
    if (alpha256 >= 256) { dst = c; return; }

    int sr = (c >> 16) & 255, sg = (c >> 8) & 255, sb = c & 255;
    int dr = (dst >> 16) & 255, dg = (dst >> 8) & 255, db = dst & 255;

    int r = dr + (((sr - dr) * alpha256) >> 8);
    int g = dg + (((sg - dg) * alpha256) >> 8);
    int b = db + (((sb - db) * alpha256) >> 8);

    dst = (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

void Graphics::blend(int x, int y, Color c, float a) {
    if (a <= 0.f) return;
    blendFast(x, y, c, int(a * 256.f + 0.5f));
}

void Graphics::fillRect(int x, int y, int w, int h, Color c, float a) {
    if (a <= 0.f || w <= 0 || h <= 0) return;
    const int alpha256 = int(a * 256.f + 0.5f);
    const int xend = std::min(w_, x + w);
    const int yend = std::min(h_, y + h);
    const int xstart = std::max(0, x);
    const int ystart = std::max(0, y);

    for (int j = ystart; j < yend; ++j) {
        for (int i = xstart; i < xend; ++i) {
            blendFast(i, j, c, alpha256);
        }
    }
}

void Graphics::fillGradientV(int x, int y, int w, int h, Color c0, Color c1, float a) {
    if (a <= 0.f || w <= 0 || h <= 0) return;
    const int yend = std::min(h_, y + h);
    const int ystart = std::max(0, y);
    for (int j = ystart; j < yend; ++j) {
        float t = (h > 1) ? float(j - y) / float(h - 1) : 0.f;
        Color c = lerpColor(c0, c1, t);
        fillRect(x, j, w, 1, c, a);
    }
}

void Graphics::fillGradientH(int x, int y, int w, int h, Color c0, Color c1, float a) {
    if (a <= 0.f || w <= 0 || h <= 0) return;
    const int xend = std::min(w_, x + w);
    const int xstart = std::max(0, x);
    for (int i = xstart; i < xend; ++i) {
        float t = (w > 1) ? float(i - x) / float(w - 1) : 0.f;
        Color c = lerpColor(c0, c1, t);
        fillRect(i, y, 1, h, c, a);
    }
}

void Graphics::fillRoundRect(int x, int y, int w, int h, int r, Color c, float a) {
    if (r <= 0) { fillRect(x, y, w, h, c, a); return; }
    r = std::min({r, w / 2, h / 2});

    // Fill main center regions
    fillRect(x + r, y, w - 2 * r, h, c, a);
    fillRect(x, y + r, r, h - 2 * r, c, a);
    fillRect(x + w - r, y + r, r, h - 2 * r, c, a);

    // Rounded corners with anti-aliasing
    auto corner = [&](float cx, float cy) {
        for (int j = int(cy - r - 1); j <= int(cy + r + 1); ++j) {
            for (int i = int(cx - r - 1); i <= int(cx + r + 1); ++i) {
                if (i < x || i >= x + w || j < y || j >= y + h) continue;
                // Only render inside the specific corner quadrant
                if ((cx < x + w / 2.f && i > cx) || (cx > x + w / 2.f && i < cx)) continue;
                if ((cy < y + h / 2.f && j > cy) || (cy > y + h / 2.f && j < cy)) continue;

                float dist = std::hypot(i + 0.5f - cx, j + 0.5f - cy);
                float cov = std::clamp(float(r) - dist + 0.5f, 0.f, 1.f);
                blend(i, j, c, a * cov);
            }
        }
    };

    corner(x + r + 0.5f, y + r + 0.5f);               // Top-left
    corner(x + w - r - 0.5f, y + r + 0.5f);           // Top-right
    corner(x + r + 0.5f, y + h - r - 0.5f);           // Bottom-left
    corner(x + w - r - 0.5f, y + h - r - 0.5f);       // Bottom-right
}

void Graphics::roundRect(int x, int y, int w, int h, int r, Color c, float a) {
    if (r <= 0) { rect(x, y, w, h, c, a); return; }
    r = std::min({r, w / 2, h / 2});

    // Edges
    fillRect(x + r, y, w - 2 * r, 1, c, a);
    fillRect(x + r, y + h - 1, w - 2 * r, 1, c, a);
    fillRect(x, y + r, 1, h - 2 * r, c, a);
    fillRect(x + w - 1, y + r, 1, h - 2 * r, c, a);

    // Corner arcs
    const float kPi = 3.14159265f;
    arc(x + r + 0.5f, y + r + 0.5f, r, kPi, kPi * 1.5f, c, 1.f, a);
    arc(x + w - r - 0.5f, y + r + 0.5f, r, kPi * 1.5f, kPi * 2.0f, c, 1.f, a);
    arc(x + r + 0.5f, y + h - r - 0.5f, r, kPi * 0.5f, kPi, c, 1.f, a);
    arc(x + w - r - 0.5f, y + h - r - 0.5f, r, 0.f, kPi * 0.5f, c, 1.f, a);
}

void Graphics::rect(int x, int y, int w, int h, Color c, float a) {
    fillRect(x, y, w, 1, c, a);
    fillRect(x, y + h - 1, w, 1, c, a);
    fillRect(x, y, 1, h, c, a);
    fillRect(x + w - 1, y, 1, h, c, a);
}

void Graphics::line(float x0, float y0, float x1, float y1, Color c, float a) {
    const bool steep = std::fabs(y1 - y0) > std::fabs(x1 - x0);
    if (steep) { std::swap(x0, y0); std::swap(x1, y1); }
    if (x0 > x1) { std::swap(x0, x1); std::swap(y0, y1); }
    const float dx = x1 - x0, dy = y1 - y0;
    const float grad = dx < 1e-6f ? 1.f : dy / dx;
    float y = y0;
    for (int x = int(std::floor(x0)); x <= int(std::floor(x1)); ++x, y += grad) {
        const float f = y - std::floor(y);
        const int yi = int(std::floor(y));
        if (steep) { blend(yi, x, c, a * (1.f - f)); blend(yi + 1, x, c, a * f); }
        else { blend(x, yi, c, a * (1.f - f)); blend(x, yi + 1, c, a * f); }
    }
}

void Graphics::thickLine(float x0, float y0, float x1, float y1, Color c, float thick, float a) {
    if (thick <= 1.f) { line(x0, y0, x1, y1, c, a); return; }
    float dx = x1 - x0, dy = y1 - y0;
    float len = std::hypot(dx, dy);
    if (len < 1e-6f) return;
    float nx = -dy / len * (thick * 0.5f);
    float ny = dx / len * (thick * 0.5f);

    int steps = int(thick * 2);
    for (int i = 0; i < steps; ++i) {
        float t = (steps > 1) ? (float(i) / (steps - 1) - 0.5f) * 2.f : 0.f;
        line(x0 + nx * t, y0 + ny * t, x1 + nx * t, y1 + ny * t, c, a);
    }
}

void Graphics::arc(float cx, float cy, float r, float a0, float a1, Color c, float thick, float a) {
    const int n = std::max(12, int(std::fabs(a1 - a0) * r));
    const float halfThick = thick * 0.5f;
    const int steps = std::max(1, int(thick * 2.f));

    for (int k = 0; k < steps; ++k) {
        const float offset = -halfThick + (float(k) + 0.5f) * (thick / steps);
        const float rr = r + offset;
        if (rr <= 0.f) continue;
        float px = cx + std::cos(a0) * rr, py = cy + std::sin(a0) * rr;
        for (int i = 1; i <= n; ++i) {
            const float angle = a0 + (a1 - a0) * i / n;
            const float x = cx + std::cos(angle) * rr, y = cy + std::sin(angle) * rr;
            line(px, py, x, y, c, a);
            px = x; py = y;
        }
    }
}

void Graphics::fillCircle(float cx, float cy, float r, Color c, float a) {
    for (int y = int(cy - r - 1); y <= int(cy + r + 1); ++y)
        for (int x = int(cx - r - 1); x <= int(cx + r + 1); ++x) {
            const float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
            const float cov = std::clamp(r - d + 0.5f, 0.f, 1.f);
            blend(x, y, c, a * cov);
        }
}

void Graphics::circle(float cx, float cy, float r, Color c, float thick, float a) {
    arc(cx, cy, r, 0.f, 2.f * 3.14159265f, c, thick, a);
}

void Graphics::text(int x, int y, const std::string& s, Color c, int scale, int align) {
    if (align == 1) x -= textWidth(s, scale) / 2;
    else if (align == 2) x -= textWidth(s, scale);
    for (char ch : s) {
        if (ch >= 'a' && ch <= 'z') ch = char(ch - 32);
        if (ch < 0x20 || ch > 0x5F) ch = '?';
        const uint8_t* g = kFont[ch - 0x20];
        for (int col = 0; col < 5; ++col)
            for (int row = 0; row < 7; ++row)
                if (g[col] & (1 << row)) fillRect(x + col * scale, y + row * scale, scale, scale, c);
        x += 6 * scale;
    }
}

bool Graphics::writePpm(const char* path) const {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w_, h_);
    for (uint32_t p : px_) {
        const unsigned char rgbv[3] = {(unsigned char)(p >> 16), (unsigned char)(p >> 8), (unsigned char)p};
        std::fwrite(rgbv, 1, 3, f);
    }
    std::fclose(f);
    return true;
}

}  // namespace kikset::gui
