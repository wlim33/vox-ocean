#pragma once
#include <string>
#include "io/Image.h"
namespace vox {
// Nearest-neighbor upscale by `scale` (values below 1 are clamped to 1), then
// write an RGB8 PNG. Returns false on a malformed image or a write failure.
bool write_png(const std::string& path, const RgbImage& img, int scale = 1);
}
