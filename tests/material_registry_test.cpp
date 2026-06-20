#include <gtest/gtest.h>
#include "voxel/MaterialRegistry.h"
#include "voxel/VoxelWorld.h"

using namespace vox;

TEST(MaterialRegistry, TableSizeMatchesEnum) {
    EXPECT_EQ(kMaterials.size(), (size_t)kNumMaterials);
}

TEST(MaterialRegistry, EnumSizeIsThirteen) {
    EXPECT_EQ((int)vox::kNumMaterials, 13);
}

TEST(MaterialRegistry, SteamIsRisingGas) {
    using namespace vox;
    const MaterialProps& s = material_props(VoxMat::Steam);
    EXPECT_EQ(s.phase, Phase::Gas);
    EXPECT_TRUE(s.movable);
    EXPECT_LT(s.density, material_props(VoxMat::Air).density);   // lighter than air -> rises
}

TEST(MaterialRegistry, CombustionMaterialProps) {
    using namespace vox;
    const MaterialProps& fire = material_props(VoxMat::Fire);
    EXPECT_EQ(fire.phase, Phase::Fire);
    EXPECT_FALSE(fire.movable);                                   // fire sits on the fuel
    const MaterialProps& smoke = material_props(VoxMat::Smoke);
    EXPECT_EQ(smoke.phase, Phase::Gas);
    EXPECT_TRUE(smoke.movable);
    EXPECT_LT(smoke.density, material_props(VoxMat::Air).density); // lighter than air -> rises
    const MaterialProps& ash = material_props(VoxMat::Ash);
    EXPECT_EQ(ash.phase, Phase::Granular);
    EXPECT_TRUE(ash.movable);
    EXPECT_GT(ash.density, material_props(VoxMat::Water).density); // heavier than water -> sinks/falls
    EXPECT_LT(ash.fluidity, 0.5f);                                // repose, not leveling
}

TEST(MaterialRegistry, BubbleBetweenAirAndWater) {
    const MaterialProps& b = material_props(VoxMat::Bubble);
    EXPECT_EQ(b.phase, Phase::Gas);
    EXPECT_TRUE(b.movable);
    EXPECT_GT(b.density, material_props(VoxMat::Air).density);    // heavier than air → won't escape into air
    EXPECT_LT(b.density, material_props(VoxMat::Water).density);  // lighter than water → rises
    EXPECT_GE(b.fluidity, 0.5f);                                 // levels into a flat pool
}

TEST(MaterialRegistry, PhasesAreCorrect) {
    EXPECT_EQ(material_props(VoxMat::Air).phase,       Phase::Empty);
    EXPECT_EQ(material_props(VoxMat::SandGrain).phase,  Phase::Granular);
    // Terrain + entity materials are immovable barriers in SP1.
    for (VoxMat m : {VoxMat::Sand, VoxMat::Rock, VoxMat::Boat, VoxMat::Kelp, VoxMat::Fish})
        EXPECT_EQ(material_props(m).phase, Phase::Solid);
}

TEST(MaterialRegistry, FillPaletteProjectsColorColumn) {
    float rgb[3 * kNumMaterials];
    fill_palette(rgb);
    for (size_t i = 0; i < (size_t)kNumMaterials; ++i) {
        EXPECT_FLOAT_EQ(rgb[3*i+0], kMaterials[i].r);
        EXPECT_FLOAT_EQ(rgb[3*i+1], kMaterials[i].g);
        EXPECT_FLOAT_EQ(rgb[3*i+2], kMaterials[i].b);
    }
}

TEST(MaterialRegistry, SandGrainTintDiffersFromTerrainSand) {
    const auto& g = material_props(VoxMat::SandGrain);
    const auto& s = material_props(VoxMat::Sand);
    EXPECT_TRUE(g.r != s.r || g.g != s.g || g.b != s.b);
}

TEST(MaterialRegistry, PhysicsScalars) {
    using namespace vox;
    // Water is a movable liquid, denser than air, lighter than sand.
    EXPECT_EQ(material_props(VoxMat::Water).phase, Phase::Liquid);
    EXPECT_TRUE(material_props(VoxMat::Water).movable);
    EXPECT_TRUE(material_props(VoxMat::Water).fluidity >= 0.5f);     // levels
    EXPECT_TRUE(material_props(VoxMat::SandGrain).fluidity < 0.5f);  // repose
    EXPECT_GT(material_props(VoxMat::SandGrain).density, material_props(VoxMat::Water).density);
    EXPECT_GT(material_props(VoxMat::Water).density,     material_props(VoxMat::Air).density);
    EXPECT_GT(material_props(VoxMat::Air).density, 0.0f);            // air participates in ordering
    // Solids/terrain are pinned.
    for (VoxMat m : {VoxMat::Rock, VoxMat::Sand, VoxMat::Boat, VoxMat::Kelp, VoxMat::Fish})
        EXPECT_FALSE(material_props(m).movable);
    EXPECT_TRUE(material_props(VoxMat::Air).movable);
    EXPECT_TRUE(material_props(VoxMat::SandGrain).movable);
}
