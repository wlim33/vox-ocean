#include <gtest/gtest.h>
#include <string_view>
#include "voxel/MaterialRegistry.h"
#include "voxel/VoxelWorld.h"

using namespace vox;

TEST(MaterialRegistry, TableSizeMatchesEnum) {
    EXPECT_EQ(kMaterials.size(), (size_t)kNumMaterials);
}

TEST(MaterialRegistry, EnumSizeIsNineteen) {
    EXPECT_EQ((int)vox::kNumMaterials, 19);
}

TEST(MaterialRegistry, LavaSinksAndIsViscous) {
    using namespace vox;
    const MaterialProps& l = material_props(VoxMat::Lava);
    EXPECT_EQ(l.phase, Phase::Liquid);
    EXPECT_TRUE(l.movable);
    EXPECT_GT(l.density, material_props(VoxMat::Water).density);  // denser than water -> sinks
    EXPECT_LT(l.fluidity, 0.5f);                                 // repose -> viscous mound
}

TEST(MaterialRegistry, LavaIsDarkRedNotYellow) {
    using namespace vox;
    // The renderer draws lava emissively (palette * glow, then ACES). A green channel
    // anywhere near the red channel tonemaps to yellow/orange, so the palette column
    // must be a strongly red-dominant dark red for lava to read as dark red on screen.
    const MaterialProps& l = material_props(VoxMat::Lava);
    EXPECT_GT(l.r, 0.30f);            // a visible red
    EXPECT_LT(l.g, 0.12f);            // little green  -> not yellow/orange
    EXPECT_LT(l.b, 0.12f);            // little blue
    EXPECT_GT(l.r, 2.5f * l.g);       // red strongly dominates green
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

TEST(MaterialRegistry, NamesMatchEnumOrder) {
    EXPECT_EQ(vox::material_name(vox::VoxMat::Air),   "Air");
    EXPECT_EQ(vox::material_name(vox::VoxMat::Water), "Water");
    EXPECT_EQ(vox::material_name(vox::VoxMat::Rock),  "Rock");
    EXPECT_EQ(vox::material_name(vox::VoxMat::Lava),  "Lava");
}

TEST(MaterialRegistry, NamesCoverEveryMaterial) {
    for (int i = 0; i < vox::kNumMaterials; ++i)
        EXPECT_FALSE(vox::material_name((vox::VoxMat)i).empty());
}

TEST(MaterialRegistry, ThermalFieldsAndTags) {
    using namespace vox;
    for (int i = 0; i < kNumMaterials; ++i) {
        EXPECT_GE(kMaterials[i].conductivity, 0.0f);
        EXPECT_LE(kMaterials[i].conductivity, 1.0f);
    }
    EXPECT_GT(material_props(VoxMat::Lava).emit_temp, 0);
    EXPECT_GT(material_props(VoxMat::Fire).emit_temp, 0);
    EXPECT_EQ(material_props(VoxMat::Water).emit_temp, -1);
    EXPECT_TRUE(material_has_tag(VoxMat::Boat, MatTag::Flammable));
    EXPECT_FALSE(material_has_tag(VoxMat::Water, MatTag::Flammable));
    EXPECT_TRUE(material_has_tag(VoxMat::Rock, MatTag::Corrodible));
}
