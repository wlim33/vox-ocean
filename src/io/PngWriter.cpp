#include "io/PngWriter.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <vector>

namespace vox {

std::expected<void, PngError> write_png(const std::string& path, const RgbImage& img, int scale) {
    if (img.w <= 0 || img.h <= 0 ||
        img.rgb.size() != (size_t)img.w * img.h * 3)
        return std::unexpected(PngError::MalformedImage);
    if (scale < 1) scale = 1;
    if (scale == 1) {
        if (stbi_write_png(path.c_str(), img.w, img.h, 3, img.rgb.data(), img.w * 3) == 0)
            return std::unexpected(PngError::WriteFailed);
        return {};
    }

    const int W = img.w * scale, H = img.h * scale;
    std::vector<uint8_t> up((size_t)W * H * 3);
    for (int y = 0; y < H; ++y) {
        const uint8_t* src = &img.rgb[(size_t)(y / scale) * img.w * 3];
        uint8_t* dst = &up[(size_t)y * W * 3];
        for (int x = 0; x < W; ++x) {
            const uint8_t* s = src + (size_t)(x / scale) * 3;
            *dst++ = s[0]; *dst++ = s[1]; *dst++ = s[2];
        }
    }
    if (stbi_write_png(path.c_str(), W, H, 3, up.data(), W * 3) == 0)
        return std::unexpected(PngError::WriteFailed);
    return {};
}

}
