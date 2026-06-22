#include <gtest/gtest.h>
#include "io/PngWriter.h"
#include <fstream>
#include <iterator>
#include <vector>

static std::vector<uint8_t> read_bytes(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

TEST(PngWriter, WritesPngSignature) {
    vox::RgbImage img{2, 2, std::vector<uint8_t>(2 * 2 * 3, 128)};
    std::string path = std::string(testing::TempDir()) + "/pw_sig.png";
    ASSERT_TRUE(vox::write_png(path, img).has_value());
    auto b = read_bytes(path);
    ASSERT_GE(b.size(), 8u);
    const uint8_t sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    for (int i = 0; i < 8; ++i) EXPECT_EQ(b[i], sig[i]) << "byte " << i;
}

TEST(PngWriter, ScaleMultipliesIhdrDimensions) {
    vox::RgbImage img{2, 3, std::vector<uint8_t>(2 * 3 * 3, 64)};
    std::string path = std::string(testing::TempDir()) + "/pw_scale.png";
    ASSERT_TRUE(vox::write_png(path, img, 4).has_value());
    auto b = read_bytes(path);
    auto be32 = [&](int o) -> uint32_t {
        return (uint32_t(b[o]) << 24) | (uint32_t(b[o+1]) << 16) |
               (uint32_t(b[o+2]) << 8) | uint32_t(b[o+3]);
    };
    EXPECT_EQ(be32(16), 2 * 4);   // IHDR width  @ offset 16
    EXPECT_EQ(be32(20), 3 * 4);   // IHDR height @ offset 20
}

TEST(PngWriter, RejectsMalformed) {
    vox::RgbImage bad{2, 2, std::vector<uint8_t>(3, 0)};  // buffer too small
    auto bad_result = vox::write_png(std::string(testing::TempDir()) + "/pw_bad.png", bad);
    EXPECT_FALSE(bad_result.has_value());
    EXPECT_EQ(bad_result.error(), vox::PngError::MalformedImage);
}
