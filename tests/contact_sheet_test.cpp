#include <gtest/gtest.h>
#include "io/ContactSheet.h"

TEST(ContactSheet, RowLayoutDimensions) {
    vox::RgbImage a{10, 8, std::vector<uint8_t>(10 * 8 * 3, 100)};
    vox::RgbImage b{6, 12, std::vector<uint8_t>(6 * 12 * 3, 50)};
    auto s = vox::make_contact_sheet({a, b}, {"TOP +Y", "SIDE +X"}, 4, 2);
    EXPECT_EQ(s.w, 10 + 4 + 6);     // widths + one gap
    EXPECT_EQ(s.h, 5 * 2 + 4 + 12); // label bar (FH*scale + 4) + tallest cell
}

TEST(ContactSheet, CopiesCellPixels) {
    vox::RgbImage a{4, 4, std::vector<uint8_t>(4 * 4 * 3, 0)};
    a.rgb[0] = 200; a.rgb[1] = 100; a.rgb[2] = 50;   // top-left pixel
    auto s = vox::make_contact_sheet({a}, {""}, 0, 2);
    int bar = 5 * 2 + 4;
    size_t i = ((size_t)bar * s.w + 0) * 3;          // cell origin sits below the bar
    EXPECT_EQ(s.rgb[i + 0], 200);
    EXPECT_EQ(s.rgb[i + 1], 100);
    EXPECT_EQ(s.rgb[i + 2], 50);
}

TEST(ContactSheet, DrawsLabelInk) {
    vox::RgbImage a{20, 8, std::vector<uint8_t>(20 * 8 * 3, 0)};
    auto s = vox::make_contact_sheet({a}, {"T"}, 0, 2);
    int bar = 5 * 2 + 4;
    bool ink = false;
    for (int y = 0; y < bar && !ink; ++y)
        for (int x = 0; x < s.w; ++x) {
            size_t i = ((size_t)y * s.w + x) * 3;
            if (s.rgb[i] == 255 && s.rgb[i + 1] == 255 && s.rgb[i + 2] == 255) { ink = true; break; }
        }
    EXPECT_TRUE(ink);
}
