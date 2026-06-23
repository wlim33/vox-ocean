#include "voxel/Brush.h"
#include "voxel/VoxelWorld.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <vector>

TEST(Brush, RadiusZeroIsSingleCenterCell) {
    vox::VoxelWorld grid({8, 8, 1.0f, 1.0f, 2.0f});
    uint32_t c = (uint32_t)grid.cell_index(4, 4, 4);
    std::vector<uint32_t> out;
    vox::sphere_cells(grid, c, 0, out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], c);
}

TEST(Brush, RadiusOneIsSevenCells) {
    vox::VoxelWorld grid({8, 8, 1.0f, 1.0f, 2.0f});
    uint32_t c = (uint32_t)grid.cell_index(4, 4, 4);
    std::vector<uint32_t> out;
    vox::sphere_cells(grid, c, 1, out);            // center + 6 face neighbours (d^2 <= 1)
    EXPECT_EQ(out.size(), 7u);
    EXPECT_NE(std::find(out.begin(), out.end(), c), out.end());
}

TEST(Brush, RadiusTwoInteriorHas33Cells) {
    vox::VoxelWorld grid({8, 8, 1.0f, 1.0f, 2.0f});  // 4+/-2 stays in [0,8)
    uint32_t c = (uint32_t)grid.cell_index(4, 4, 4);
    std::vector<uint32_t> out;
    vox::sphere_cells(grid, c, 2, out);            // |{d^2 <= 4}| = 1+6+12+8+6
    EXPECT_EQ(out.size(), 33u);
    // all unique
    std::vector<uint32_t> sorted = out;
    std::sort(sorted.begin(), sorted.end());
    EXPECT_EQ(std::unique(sorted.begin(), sorted.end()), sorted.end());
}

TEST(Brush, ClampsAtGridCorner) {
    vox::VoxelWorld grid({8, 8, 1.0f, 1.0f, 2.0f});
    uint32_t c = (uint32_t)grid.cell_index(0, 0, 0);  // corner: negative offsets clip
    std::vector<uint32_t> out;
    vox::sphere_cells(grid, c, 2, out);
    EXPECT_EQ(out.size(), 11u);   // offsets with x,y,z in [0,2], d^2<=4
    for (uint32_t cell : out) {
        int ix, iy, iz; grid.decode_cell_index((int)cell, ix, iy, iz);
        EXPECT_GE(ix, 0); EXPECT_LT(ix, grid.params().extent);
        EXPECT_GE(iy, 0); EXPECT_LT(iy, grid.params().height_cells);
        EXPECT_GE(iz, 0); EXPECT_LT(iz, grid.params().extent);
    }
}
