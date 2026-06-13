#include "voxel_grid.h"
#include <gtest/gtest.h>

// extent 4, 48 cells, 0.5 m voxels, 0.25 m steps, 10 m base (matches voxel_world_test)
static VoxelGridDesc desc() { return VoxelGridDesc{4, 48, 0.5f, 0.25f, 10.0f}; }

TEST(VoxelGrid, CellIndexMatchesUploadLayout) {
    auto g = desc();
    EXPECT_EQ(vg_cell_index(g, 0, 0, 0), 0);
    EXPECT_EQ(vg_cell_index(g, 1, 0, 0), 1);
    EXPECT_EQ(vg_cell_index(g, 0, 1, 0), 4);
    EXPECT_EQ(vg_cell_index(g, 0, 0, 1), 4 * 48);
    EXPECT_EQ(vg_cell_index(g, 1, 2, 3), (3 * 48 + 2) * 4 + 1);
}
TEST(VoxelGrid, DecodeRoundTrips) {
    auto g = desc();
    VgCell c = vg_decode_index(g, vg_cell_index(g, 1, 2, 3));
    EXPECT_EQ(c.ix, 1); EXPECT_EQ(c.iy, 2); EXPECT_EQ(c.iz, 3);
}
TEST(VoxelGrid, ColumnCenterAndExtents) {
    auto g = desc();
    EXPECT_FLOAT_EQ(vg_half_patch(g), 1.0f);
    EXPECT_FLOAT_EQ(vg_column_center(g, 0), -0.75f);
    EXPECT_FLOAT_EQ(vg_column_center(g, 1), -0.25f);
    EXPECT_FLOAT_EQ(vg_column_center(g, 3),  0.75f);
    EXPECT_FLOAT_EQ(vg_world_top_y(g), -10.0f + 48 * 0.25f);
}
TEST(VoxelGrid, QuantizeFloorsToStep) {
    auto g = desc();
    EXPECT_FLOAT_EQ(vg_quantize_height(g, 0.0f),   0.0f);
    EXPECT_FLOAT_EQ(vg_quantize_height(g, 0.24f),  0.0f);
    EXPECT_FLOAT_EQ(vg_quantize_height(g, 0.26f),  0.25f);
    EXPECT_FLOAT_EQ(vg_quantize_height(g, -0.30f), -0.5f);
    EXPECT_FLOAT_EQ(vg_quantize_height(g, -50.0f), -9.75f);
}
TEST(VoxelGrid, WaterTopCellCountsFromBase) {
    auto g = desc();
    EXPECT_EQ(vg_water_top_cell(g, 0.0f),  40);
    EXPECT_EQ(vg_water_top_cell(g, 0.26f), 41);
    EXPECT_EQ(vg_water_top_cell(g, -50.0f), 1);
    EXPECT_EQ(vg_water_top_cell(g, 1e6f),  48);
}
TEST(VoxelGrid, ColumnAtClampsToGrid) {
    auto g = desc();   // half_patch 1.0, spans [-1, 1]
    VgCol a = vg_column_at(g, -0.9f, -0.9f); EXPECT_EQ(a.ix, 0); EXPECT_EQ(a.iz, 0);
    VgCol b = vg_column_at(g,  0.9f,  0.9f); EXPECT_EQ(b.ix, 3); EXPECT_EQ(b.iz, 3);
    VgCol c = vg_column_at(g, -5.0f,  5.0f); EXPECT_EQ(c.ix, 0); EXPECT_EQ(c.iz, 3);
}
