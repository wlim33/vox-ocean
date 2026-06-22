#pragma once
#include <string>
#include <expected>
#include "io/Image.h"
namespace vox {
enum class PngError { MalformedImage, WriteFailed };
// Nearest-neighbor upscale by `scale` (values below 1 are clamped to 1), then
// write an RGB8 PNG. Returns PngError::MalformedImage for a bad image,
// PngError::WriteFailed if stb fails to write the file.
std::expected<void, PngError> write_png(const std::string& path, const RgbImage& img, int scale = 1);
}
