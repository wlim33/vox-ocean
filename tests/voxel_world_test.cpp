#include "voxel/VoxelWorld.h"
#include "shader_types.h"
#include <gtest/gtest.h>

// Lockstep: this enum and the MAT_* constants compiled into the shaders must
// agree — this is the single checkpoint.
static_assert((int)vox::VoxMat::Air   == MAT_AIR);
static_assert((int)vox::VoxMat::Water == MAT_WATER);
static_assert((int)vox::VoxMat::Sand  == MAT_SAND);
static_assert((int)vox::VoxMat::Rock  == MAT_ROCK);
static_assert((int)vox::VoxMat::Boat  == MAT_BOAT);
static_assert((int)vox::VoxMat::Kelp  == MAT_KELP);
static_assert((int)vox::VoxMat::Fish  == MAT_FISH);
static_assert((int)vox::VoxMat::SandGrain == MAT_SANDGRAIN);
static_assert((int)vox::VoxMat::Bubble == MAT_BUBBLE);
static_assert((int)vox::VoxMat::Fire  == MAT_FIRE);
static_assert((int)vox::VoxMat::Smoke == MAT_SMOKE);
static_assert((int)vox::VoxMat::Ash   == MAT_ASH);
static_assert((int)vox::VoxMat::Steam == MAT_STEAM);
static_assert((int)vox::VoxMat::Lava == MAT_LAVA);
static_assert((int)vox::kNumMaterials == NUM_MATERIALS);

// extent 4, 48 cells, 0.5m voxels, 0.25m steps, 10m base
static vox::VoxelWorld make() { return vox::VoxelWorld({4, 48, 0.5f, 0.25f, 10.0f}); }

TEST(VoxelWorld, WorldMappingIsCentered) {
    auto w = make();                       // patch = 4 * 0.5 = 2 m, spans [-1, 1]
    EXPECT_FLOAT_EQ(w.column_center_x(0), -0.75f);
    EXPECT_FLOAT_EQ(w.column_center_x(1), -0.25f);  // interior column: catches offset errors that cancel at the edges
    EXPECT_FLOAT_EQ(w.column_center_x(3),  0.75f);
    EXPECT_FLOAT_EQ(w.column_center_z(2),  0.25f);
    EXPECT_FLOAT_EQ(w.patch_size_m(), 2.0f);
    EXPECT_EQ(w.columns(), 16);
    EXPECT_EQ(w.cells(), 16 * 48);
    EXPECT_FLOAT_EQ(w.world_top_y(), -10.0f + 48 * 0.25f);
    EXPECT_FLOAT_EQ(w.cell_bottom_y(0), -10.0f);
    EXPECT_FLOAT_EQ(w.cell_bottom_y(40), 0.0f);
}

TEST(VoxelWorld, QuantizeFloorsToStep) {
    // Floor policy (user decision): a column only rises once the wave fully
    // clears the next step — calmer crests, no half-step flicker.
    auto w = make();
    EXPECT_FLOAT_EQ(w.quantize_height(0.0f),   0.0f);
    EXPECT_FLOAT_EQ(w.quantize_height(0.24f),  0.0f);   // not yet a full step
    EXPECT_FLOAT_EQ(w.quantize_height(0.26f),  0.25f);
    EXPECT_FLOAT_EQ(w.quantize_height(-0.30f), -0.5f);  // floor goes down, not toward zero
    EXPECT_FLOAT_EQ(w.quantize_height(0.25f),  0.25f);  // exactly on boundary stays
}

TEST(VoxelWorld, QuantizeClampsToBase) {
    auto w = make();   // base at -10: never quantize below one step above the floor
    EXPECT_FLOAT_EQ(w.quantize_height(-50.0f), -9.75f);
    EXPECT_FLOAT_EQ(w.quantize_height(-9.74f), -9.75f);
}

TEST(VoxelWorld, WaterTopCellCountsFromBase) {
    auto w = make();
    EXPECT_EQ(w.water_top_cell(0.0f),   40);  // (0 + 10) / 0.25
    EXPECT_EQ(w.water_top_cell(0.24f),  40);  // quantizes down first
    EXPECT_EQ(w.water_top_cell(0.26f),  41);
    EXPECT_EQ(w.water_top_cell(-50.0f),  1);  // clamp: at least one water cell
    EXPECT_EQ(w.water_top_cell(1e6f),   48);  // clamp: grid top
}

TEST(VoxelWorld, BaseAlignsWithStepGrid) {
    // Contract: base_depth_m is an integer multiple of height_step_m, so the
    // quantize floor lands exactly on a cell boundary.
    auto w = make();
    EXPECT_FLOAT_EQ(w.cell_bottom_y(w.water_top_cell(0.0f)), 0.0f);
}

TEST(VoxelWorld, CellIndexMatchesUploadLayout) {
    auto w = make();   // x fastest, then y, then z
    EXPECT_EQ(w.cell_index(0, 0, 0), 0);
    EXPECT_EQ(w.cell_index(1, 0, 0), 1);
    EXPECT_EQ(w.cell_index(0, 1, 0), 4);
    EXPECT_EQ(w.cell_index(0, 0, 1), 4 * 48);
    EXPECT_EQ(w.cell_index(1, 2, 3), (3 * 48 + 2) * 4 + 1);
}

TEST(VoxelWorld, DecodeCellIndexRoundTrips) {
    auto w = make();   // mirror of the stamp_cells kernel decode
    int ix, iy, iz;
    w.decode_cell_index(w.cell_index(1, 2, 3), ix, iy, iz);
    EXPECT_EQ(ix, 1); EXPECT_EQ(iy, 2); EXPECT_EQ(iz, 3);
    w.decode_cell_index(w.cell_index(3, 47, 0), ix, iy, iz);
    EXPECT_EQ(ix, 3); EXPECT_EQ(iy, 47); EXPECT_EQ(iz, 0);
}
