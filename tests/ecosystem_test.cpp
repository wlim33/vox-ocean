#include "entity/Ecosystem.h"
#include "entity/StampBudget.h"
#include "voxel/VoxelWorld.h"
#include "world/World.h"
#include "core/Config.h"
#include <gtest/gtest.h>
#include <cmath>

static vox::Config eco_cfg() {
    vox::Config c;
    c.voxel.grid_extent = 64; c.voxel.height_cells = 64;
    c.voxel.voxel_size_m = 0.5f; c.voxel.height_step_m = 0.25f; c.voxel.base_depth_m = 10.0f;
    c.kelp.enabled = true; c.kelp.density = 0.03f;
    c.fish.enabled = true; c.fish.school_count = 2; c.fish.per_school = 8;
    c.entity.boat_enabled = true;
    return c;
}
static vox::VoxelWorld eco_world(const vox::Config& c) {
    return vox::VoxelWorld({c.voxel.grid_extent, c.voxel.height_cells, c.voxel.voxel_size_m,
                            c.voxel.height_step_m, c.voxel.base_depth_m});
}

TEST(Ecosystem, BuildStampStaysWithinBudget) {
    auto c = eco_cfg();
    vox::World world;
    world.configure(c);
    vox::Ecosystem eco;
    eco.rebuild_if_dirty(c, world);
    eco.update(c, 1.0f/60.0f, 0.0f, [](float, float){ return 0.0f; }, world);
    vox::StampList out; eco.build_stamp(c, eco_world(c), out);
    EXPECT_GT(out.count(), 0);
    EXPECT_LE(out.count(), vox::max_stamp_cells(c));   // never exceeds the buffer
}

TEST(Ecosystem, BoatOnlyStampsBoat) {
    auto c = eco_cfg();
    c.kelp.enabled = false; c.fish.enabled = false;
    vox::World world;
    world.configure(c);
    vox::Ecosystem eco;
    eco.rebuild_if_dirty(c, world);
    eco.update(c, 1.0f/60.0f, 0.0f, [](float, float){ return 0.0f; }, world);
    vox::StampList out; eco.build_stamp(c, eco_world(c), out);
    EXPECT_GT(out.count(), 0);
    for (int i = 0; i < out.count(); ++i)
        EXPECT_EQ((int)out.mat[i], (int)vox::VoxMat::Boat);
}

TEST(Ecosystem, DisabledEntitiesProduceNoCells) {
    auto c = eco_cfg();
    c.kelp.enabled = false; c.fish.enabled = false; c.entity.boat_enabled = false;
    vox::World world;
    world.configure(c);
    vox::Ecosystem eco;
    eco.rebuild_if_dirty(c, world);
    eco.update(c, 1.0f/60.0f, 0.0f, [](float, float){ return 0.0f; }, world);
    vox::StampList out; eco.build_stamp(c, eco_world(c), out);
    EXPECT_EQ(out.count(), 0);
}

// Recover the cached floor cell-count at the +X+Z corner, independent of the
// world-y conversion (which uses live cfg): h = (floor_top_y + base) / step.
static int corner_floor_cells(const vox::World& world, const vox::Config& c) {
    float half = 0.5f * c.voxel.grid_extent * c.voxel.voxel_size_m;
    float xz = half - 0.5f * c.voxel.voxel_size_m;             // last column center
    float y = world.floor_top_y(xz, xz);
    return (int)std::lround((y + c.voxel.base_depth_m) / c.voxel.height_step_m);
}

TEST(Ecosystem, FloorRegeneratesWhenBaseDepthChanges) {
    auto c = eco_cfg();                          // base_depth 10 -> sea 40, peak 56
    vox::World world;
    world.configure(c);
    vox::Ecosystem eco;
    eco.rebuild_if_dirty(c, world);
    int before = corner_floor_cells(world, c);
    c.voxel.base_depth_m = 6.0f;                 // sea 24, peak 40 -> shoreline lowers
    world.configure(c);
    eco.rebuild_if_dirty(c, world);
    int after = corner_floor_cells(world, c);
    EXPECT_NE(before, after);                     // stale floor would give equal counts
}

TEST(Ecosystem, FloorRegeneratesWhenHeightStepChanges) {
    auto c = eco_cfg();                          // step 0.25 -> sea 40, peak 56
    vox::World world;
    world.configure(c);
    vox::Ecosystem eco;
    eco.rebuild_if_dirty(c, world);
    int before = corner_floor_cells(world, c);
    c.voxel.height_step_m = 0.5f;                // sea 20, peak 28 -> shoreline lowers
    world.configure(c);
    eco.rebuild_if_dirty(c, world);
    int after = corner_floor_cells(world, c);
    EXPECT_NE(before, after);
}
