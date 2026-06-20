#include <gtest/gtest.h>
#include "core/Config.h"

TEST(Config, DefaultsLoadFromEmptyToml) {
    auto result = vox::load_config_from_string("");
    EXPECT_TRUE(result.warnings.empty());
}

TEST(Config, RejectsUnknownTopLevelKey) {
    auto result = vox::load_config_from_string("zorp = 1\n");
    bool has_unknown = false;
    for (auto& w : result.warnings) if (w.find("zorp") != std::string::npos) has_unknown = true;
    EXPECT_TRUE(has_unknown);
}

TEST(Config, CliOverrideAppliesAfterToml) {
    auto base = vox::load_config_from_string("");
    auto r = vox::apply_overrides(std::move(base), {"voxel.grid_extent=64"});
    EXPECT_EQ(r.config.voxel.grid_extent, 64);
}

TEST(Config, HashStableForIdenticalConfigs) {
    auto a = vox::load_config_from_string("");
    auto b = vox::load_config_from_string("");
    EXPECT_EQ(vox::config_hash(a.config), vox::config_hash(b.config));
}

TEST(Config, HashChangesWithDifferentConfig) {
    auto a = vox::load_config_from_string("");
    auto b = vox::load_config_from_string("[voxel]\ngrid_extent = 64\n");
    EXPECT_NE(vox::config_hash(a.config), vox::config_hash(b.config));
}

TEST(Config, VoxelSectionParsesAndDefaults) {
    auto r = vox::load_config_from_string("");
    EXPECT_EQ(r.config.voxel.grid_extent, 192);
    EXPECT_FLOAT_EQ(r.config.voxel.voxel_size_m, 0.5f);
    EXPECT_FLOAT_EQ(r.config.voxel.height_step_m, 0.25f);
    EXPECT_FLOAT_EQ(r.config.voxel.base_depth_m, 10.0f);

    auto r2 = vox::load_config_from_string(
        "[voxel]\ngrid_extent = 64\nvoxel_size_m = 1.0\n"
        "height_step_m = 0.5\nbase_depth_m = 4.0\n");
    EXPECT_EQ(r2.config.voxel.grid_extent, 64);
    EXPECT_FLOAT_EQ(r2.config.voxel.voxel_size_m, 1.0f);

    auto r3 = vox::apply_overrides(vox::load_config_from_string(""),
                                   {"voxel.grid_extent=128"});
    EXPECT_EQ(r3.config.voxel.grid_extent, 128);
}

TEST(Config, VoxelParamsAffectHash) {
    auto a = vox::load_config_from_string("").config;
    auto b = a; b.voxel.grid_extent = 96;
    EXPECT_NE(vox::config_hash(a), vox::config_hash(b));
}

TEST(Config, MalformedOverrideValueWarnsInsteadOfThrowing) {
    auto r = vox::apply_overrides(vox::load_config_from_string(""),
                                  {"voxel.grid_extent=banana"});
    EXPECT_EQ(r.config.voxel.grid_extent, 192);   // unchanged
    EXPECT_FALSE(r.warnings.empty());
}

TEST(Config, VoxelValuesAreClamped) {
    auto r = vox::load_config_from_string("[voxel]\ngrid_extent = 0\nvoxel_size_m = 0.0\n");
    EXPECT_EQ(r.config.voxel.grid_extent, 8);
    EXPECT_FLOAT_EQ(r.config.voxel.voxel_size_m, 0.05f);
    EXPECT_FALSE(r.warnings.empty());
    auto r2 = vox::apply_overrides(vox::load_config_from_string(""), {"voxel.grid_extent=-5"});
    EXPECT_EQ(r2.config.voxel.grid_extent, 8);
}

TEST(Config, VoxelWorldKnobsParse) {
    auto r = vox::load_config_from_string(R"([voxel]
height_cells = 96
floor_seed = 1234
[march]
max_steps = 256
render_scale = 0.5
)");
    EXPECT_EQ(r.config.voxel.height_cells, 96);
    EXPECT_EQ(r.config.voxel.floor_seed, 1234);
    EXPECT_EQ(r.config.march.max_steps, 256);
    EXPECT_FLOAT_EQ(r.config.march.render_scale, 0.5f);
}

TEST(Config, MarchKnobsClampAndWarn) {
    auto r = vox::load_config_from_string(R"([march]
max_steps = 99999
render_scale = 0.05
)");
    EXPECT_EQ(r.config.march.max_steps, 4096);
    EXPECT_FLOAT_EQ(r.config.march.render_scale, 0.25f);
    EXPECT_EQ(r.warnings.size(), 2u);
}

TEST(Config, VoxelWorldOverrides) {
    auto r = vox::apply_overrides(vox::load_config_from_string(""),
        {"voxel.height_cells=128", "voxel.floor_seed=42",
         "march.max_steps=128", "march.render_scale=0.75"});
    EXPECT_EQ(r.config.voxel.height_cells, 128);
    EXPECT_EQ(r.config.voxel.floor_seed, 42);
    EXPECT_EQ(r.config.march.max_steps, 128);
    EXPECT_FLOAT_EQ(r.config.march.render_scale, 0.75f);
}

TEST(Config, HashCoversVoxelWorldKnobs) {
    vox::Config a, b;
    EXPECT_EQ(vox::config_hash(a), vox::config_hash(b));
    b.voxel.height_cells = 128;
    EXPECT_NE(vox::config_hash(a), vox::config_hash(b));
    b = a; b.march.render_scale = 0.5f;
    EXPECT_NE(vox::config_hash(a), vox::config_hash(b));
    b = a; b.voxel.floor_seed = 9;
    EXPECT_NE(vox::config_hash(a), vox::config_hash(b));
    b = a; b.march.max_steps = 64;
    EXPECT_NE(vox::config_hash(a), vox::config_hash(b));
}

TEST(Config, HashCoversWaterIor) {
    vox::Config a, b;
    EXPECT_EQ(vox::config_hash(a), vox::config_hash(b));
    b.shading.water_ior = 1.5f;
    EXPECT_NE(vox::config_hash(a), vox::config_hash(b));
}

TEST(Config, EntityKnobsParseClampOverrideHash) {
    auto r = vox::load_config_from_string(R"([entity]
boat_enabled = false
boat_speed_mps = 2.5
wake_amp = 0.5
)");
    EXPECT_FALSE(r.config.entity.boat_enabled);
    EXPECT_FLOAT_EQ(r.config.entity.boat_speed_mps, 2.5f);
    EXPECT_FLOAT_EQ(r.config.entity.wake_amp, 0.5f);

    auto o = vox::apply_overrides(vox::load_config_from_string(""),
        {"entity.boat_enabled=false", "entity.boat_speed_mps=9"});
    EXPECT_FALSE(o.config.entity.boat_enabled);
    EXPECT_FLOAT_EQ(o.config.entity.boat_speed_mps, 5.0f);   // clamped [0,5]
    EXPECT_EQ(o.warnings.size(), 1u);

    vox::Config a, b;
    b.entity.boat_enabled = false;
    EXPECT_NE(vox::config_hash(a), vox::config_hash(b));
    b = a; b.entity.boat_speed_mps = 3.0f;
    EXPECT_NE(vox::config_hash(a), vox::config_hash(b));
    b = a; b.entity.wake_amp = 1.0f;
    EXPECT_NE(vox::config_hash(a), vox::config_hash(b));
}

TEST(Config, KelpKnobsParseClampOverrideHash) {
    auto r = vox::load_config_from_string(R"([kelp]
enabled = false
density = 0.05
max_stalks = 5000
max_height_m = 8.0
sway_strength = 1.0
sway_ambient = 0.3
seed = 7
)");
    EXPECT_FALSE(r.config.kelp.enabled);
    EXPECT_FLOAT_EQ(r.config.kelp.density, 0.05f);
    EXPECT_EQ(r.config.kelp.max_stalks, 5000);
    EXPECT_FLOAT_EQ(r.config.kelp.max_height_m, 8.0f);

    auto o = vox::apply_overrides(vox::load_config_from_string(""),
        {"kelp.density=9", "kelp.enabled=false", "kelp.max_stalks=4000"});
    EXPECT_FLOAT_EQ(o.config.kelp.density, 0.3f);   // clamped [0,0.3]
    EXPECT_FALSE(o.config.kelp.enabled);
    EXPECT_EQ(o.config.kelp.max_stalks, 4000);
    EXPECT_EQ(o.warnings.size(), 1u);               // only density=9 warns; max_stalks in range

    vox::Config a, b;
    b.kelp.density = 0.1f;
    EXPECT_NE(vox::config_hash(a), vox::config_hash(b));
    b = a; b.kelp.seed = 5;
    EXPECT_NE(vox::config_hash(a), vox::config_hash(b));
    b = a; b.kelp.max_stalks = 100;
    EXPECT_NE(vox::config_hash(a), vox::config_hash(b));
}

TEST(Config, FishKnobsParseClampOverrideHash) {
    auto r = vox::load_config_from_string(R"([fish]
enabled = false
school_count = 6
per_school = 30
speed_mps = 3.0
depth_frac = 0.7
spread_m = 5.0
seed = 9
)");
    EXPECT_FALSE(r.config.fish.enabled);
    EXPECT_EQ(r.config.fish.school_count, 6);
    EXPECT_EQ(r.config.fish.per_school, 30);
    EXPECT_FLOAT_EQ(r.config.fish.speed_mps, 3.0f);

    auto o = vox::apply_overrides(vox::load_config_from_string(""),
        {"fish.school_count=99"});
    EXPECT_EQ(o.config.fish.school_count, 32);   // clamped [0,32]
    EXPECT_EQ(o.warnings.size(), 1u);

    vox::Config a, b;
    b.fish.school_count = 8;
    EXPECT_NE(vox::config_hash(a), vox::config_hash(b));
    b = a; b.fish.depth_frac = 0.25f;
    EXPECT_NE(vox::config_hash(a), vox::config_hash(b));
}

TEST(Config, RenderBackendParsesAndOverrides) {
    auto r = vox::load_config_from_string("[render]\nbackend = \"foo\"\n");
    EXPECT_EQ(r.config.render.backend, "foo");
    auto d = vox::load_config_from_string("");
    EXPECT_EQ(d.config.render.backend, "raymarch");
    auto o = vox::apply_overrides(vox::load_config_from_string(""), {"render.backend=bar"});
    EXPECT_EQ(o.config.render.backend, "bar");
    EXPECT_NE(vox::config_hash(o.config), vox::config_hash(d.config));
}

TEST(Config, ParsesSandSection) {
    auto r = vox::load_config_from_string(
        "[sand]\nenabled = true\nspawn_radius = 10\nspawn_thickness = 20\n");
    EXPECT_TRUE(r.config.sand.enabled);
    EXPECT_EQ(r.config.sand.spawn_radius, 10);
    EXPECT_EQ(r.config.sand.spawn_thickness, 20);
}

TEST(Config, SandDefaultsDisabled) {
    auto r = vox::load_config_from_string("");
    EXPECT_FALSE(r.config.sand.enabled);
}

TEST(Config, SandOverrideViaSet) {
    auto r = vox::apply_overrides(vox::load_config_from_string(""),
                                  {"sand.enabled=true", "sand.spawn_radius=3"});
    EXPECT_TRUE(r.config.sand.enabled);
    EXPECT_EQ(r.config.sand.spawn_radius, 3);
}

TEST(Config, ParsesBubbleSection) {
    auto r = vox::load_config_from_string(
        "[bubble]\nenabled = true\nspawn_radius = 5\nspawn_depth = 6\n");
    EXPECT_TRUE(r.config.bubble.enabled);
    EXPECT_EQ(r.config.bubble.spawn_radius, 5);
    EXPECT_EQ(r.config.bubble.spawn_depth, 6);
}

TEST(Config, BubbleDefaultsDisabled) {
    auto r = vox::load_config_from_string("");
    EXPECT_FALSE(r.config.bubble.enabled);
}

TEST(Config, BubbleOverrideViaSet) {
    auto r = vox::apply_overrides(vox::load_config_from_string(""),
                                  {"bubble.enabled=true", "bubble.spawn_radius=3"});
    EXPECT_TRUE(r.config.bubble.enabled);
    EXPECT_EQ(r.config.bubble.spawn_radius, 3);
}

TEST(Config, ParsesFireSection) {
    auto r = vox::load_config_from_string(
        "[fire]\nenabled = true\nspawn_radius = 5\nspawn_height = 10\n"
        "burn_out_chance = 0.2\nsmoke_chance = 0.5\nsmoke_dissipate_chance = 0.1\nignite_scale = 2.0\n");
    EXPECT_TRUE(r.config.fire.enabled);
    EXPECT_EQ(r.config.fire.spawn_radius, 5);
    EXPECT_EQ(r.config.fire.spawn_height, 10);
    EXPECT_FLOAT_EQ(r.config.fire.burn_out_chance, 0.2f);
    EXPECT_FLOAT_EQ(r.config.fire.smoke_chance, 0.5f);
    EXPECT_FLOAT_EQ(r.config.fire.smoke_dissipate_chance, 0.1f);
    EXPECT_FLOAT_EQ(r.config.fire.ignite_scale, 2.0f);
}

TEST(Config, FireDefaultsDisabled) {
    auto r = vox::load_config_from_string("");
    EXPECT_FALSE(r.config.fire.enabled);
}

TEST(Config, FireOverrideViaSet) {
    auto r = vox::apply_overrides(vox::load_config_from_string(""),
                                  {"fire.enabled=true", "fire.spawn_radius=3"});
    EXPECT_TRUE(r.config.fire.enabled);
    EXPECT_EQ(r.config.fire.spawn_radius, 3);
}

