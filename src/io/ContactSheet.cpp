#include "io/ContactSheet.h"
#include <algorithm>
#include <cstdint>

namespace vox {
namespace {

constexpr int FW = 3, FH = 5;   // glyph cell: 3 wide, 5 tall

struct Glyph { char c; uint8_t rows[FH]; };   // each row: low FW bits, 1 = ink
const Glyph kFont[] = {
    {' ', {0, 0, 0, 0, 0}},
    {'+', {0b000, 0b010, 0b111, 0b010, 0b000}},
    {'-', {0b000, 0b000, 0b111, 0b000, 0b000}},
    {'T', {0b111, 0b010, 0b010, 0b010, 0b010}},
    {'O', {0b111, 0b101, 0b101, 0b101, 0b111}},
    {'P', {0b111, 0b101, 0b111, 0b100, 0b100}},
    {'F', {0b111, 0b100, 0b111, 0b100, 0b100}},
    {'R', {0b111, 0b101, 0b111, 0b110, 0b101}},
    {'N', {0b101, 0b111, 0b111, 0b101, 0b101}},
    {'S', {0b111, 0b100, 0b111, 0b001, 0b111}},
    {'I', {0b111, 0b010, 0b010, 0b010, 0b111}},
    {'D', {0b110, 0b101, 0b101, 0b101, 0b110}},
    {'E', {0b111, 0b100, 0b111, 0b100, 0b111}},
    {'B', {0b110, 0b101, 0b110, 0b101, 0b110}},
    {'X', {0b101, 0b101, 0b010, 0b101, 0b101}},
    {'Y', {0b101, 0b101, 0b010, 0b010, 0b010}},
    {'Z', {0b111, 0b001, 0b010, 0b100, 0b111}},
    {'A', {0b010, 0b101, 0b111, 0b101, 0b101}},
    {'C', {0b011, 0b100, 0b100, 0b100, 0b011}},
    {'K', {0b101, 0b110, 0b100, 0b110, 0b101}},
};

const uint8_t* glyph_for(char c) {
    for (auto& g : kFont) if (g.c == c) return g.rows;
    return kFont[0].rows;   // unknown -> space
}

void put(RgbImage& im, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || y < 0 || x >= im.w || y >= im.h) return;
    size_t i = ((size_t)y * im.w + x) * 3;
    im.rgb[i] = r; im.rgb[i + 1] = g; im.rgb[i + 2] = b;
}

void draw_text(RgbImage& im, int x, int y, const std::string& s, int scale) {
    int cx = x;
    for (char ch : s) {
        const uint8_t* rows = glyph_for(ch);
        for (int ry = 0; ry < FH; ++ry)
            for (int rx = 0; rx < FW; ++rx)
                if (rows[ry] & (1 << (FW - 1 - rx)))
                    for (int sy = 0; sy < scale; ++sy)
                        for (int sx = 0; sx < scale; ++sx)
                            put(im, cx + rx * scale + sx, y + ry * scale + sy, 255, 255, 255);
        cx += (FW + 1) * scale;
    }
}

}  // namespace

RgbImage make_contact_sheet(const std::vector<RgbImage>& cells,
                            const std::vector<std::string>& labels,
                            int gap, int label_scale) {
    const int bar = FH * label_scale + 4;
    int cellH = 0, totalW = 0;
    for (auto& c : cells) { cellH = std::max(cellH, c.h); totalW += c.w; }
    if (!cells.empty()) totalW += gap * ((int)cells.size() - 1);

    const int W = std::max(1, totalW), H = bar + std::max(1, cellH);
    RgbImage out{W, H, std::vector<uint8_t>((size_t)W * H * 3)};
    for (size_t i = 0; i < out.rgb.size(); i += 3) {   // dark-grey background
        out.rgb[i] = 20; out.rgb[i + 1] = 20; out.rgb[i + 2] = 24;
    }

    int x = 0;
    for (size_t k = 0; k < cells.size(); ++k) {
        const RgbImage& c = cells[k];
        for (int yy = 0; yy < c.h; ++yy)
            for (int xx = 0; xx < c.w; ++xx) {
                size_t s = ((size_t)yy * c.w + xx) * 3;
                size_t d = ((size_t)(bar + yy) * W + (x + xx)) * 3;
                out.rgb[d] = c.rgb[s]; out.rgb[d + 1] = c.rgb[s + 1]; out.rgb[d + 2] = c.rgb[s + 2];
            }
        if (k < labels.size()) draw_text(out, x + 2, 2, labels[k], label_scale);
        x += c.w + gap;
    }
    return out;
}

}
