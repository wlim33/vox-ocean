#include "voxel/VoxelGrid.h"
#include <gtest/gtest.h>

static vox::VoxelGrid make() { return vox::VoxelGrid({4, 0.5f, 0.25f, 10.0f}); }

TEST(VoxelGrid, WorldMappingIsCentered) {
    auto g = make();                       // patch = 4 * 0.5 = 2 m, spans [-1, 1]
    EXPECT_FLOAT_EQ(g.column_center_x(0), -0.75f);
    EXPECT_FLOAT_EQ(g.column_center_x(1), -0.25f);  // interior column: catches offset errors that cancel at the edges
    EXPECT_FLOAT_EQ(g.column_center_x(3),  0.75f);
    EXPECT_FLOAT_EQ(g.column_center_z(2),  0.25f);
    EXPECT_FLOAT_EQ(g.patch_size_m(), 2.0f);
    EXPECT_EQ(g.columns(), 16);
}
TEST(VoxelGrid, QuantizeFloorsToStep) {
    // Floor policy (user decision): a column only rises once the wave fully
    // clears the next step — calmer crests, no half-step flicker.
    auto g = make();
    EXPECT_FLOAT_EQ(g.quantize_height(0.0f),   0.0f);
    EXPECT_FLOAT_EQ(g.quantize_height(0.24f),  0.0f);   // not yet a full step
    EXPECT_FLOAT_EQ(g.quantize_height(0.26f),  0.25f);
    EXPECT_FLOAT_EQ(g.quantize_height(-0.30f), -0.5f);  // floor goes down, not toward zero
    EXPECT_FLOAT_EQ(g.quantize_height(0.25f),  0.25f);  // exactly on boundary stays
}
TEST(VoxelGrid, QuantizeClampsToBase) {
    auto g = make();   // base at -10: never quantize below one step above the floor
    EXPECT_FLOAT_EQ(g.quantize_height(-50.0f), -9.75f);
    EXPECT_FLOAT_EQ(g.quantize_height(-9.74f), -9.75f);  // lands exactly on the clamp boundary
}
