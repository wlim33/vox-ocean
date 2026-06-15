#pragma once
#include <cstdint>
#include <vector>
namespace vox {
// Packed RGB8, row-major, row 0 = top. size(rgb) == w*h*3.
struct RgbImage {
    int w = 0, h = 0;
    std::vector<uint8_t> rgb;
};
}
