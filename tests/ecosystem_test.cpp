#include "entity/Ecosystem.h"
#include "entity/StampBudget.h"
#include "voxel/VoxelWorld.h"
#include "core/Config.h"
#include <gtest/gtest.h>

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
    vox::Ecosystem eco;
    eco.rebuild_if_dirty(c);
    eco.update(c, 1.0f/60.0f, 0.0f, [](float, float){ return 0.0f; });
    vox::StampList out; eco.build_stamp(c, eco_world(c), out);
    EXPECT_GT(out.count(), 0);
    EXPECT_LE(out.count(), vox::max_stamp_cells(c));   // never exceeds the buffer
}

TEST(Ecosystem, BoatOnlyStampsBoat) {
    auto c = eco_cfg();
    c.kelp.enabled = false; c.fish.enabled = false;
    vox::Ecosystem eco; eco.rebuild_if_dirty(c);
    eco.update(c, 1.0f/60.0f, 0.0f, [](float, float){ return 0.0f; });
    vox::StampList out; eco.build_stamp(c, eco_world(c), out);
    EXPECT_GT(out.count(), 0);
    for (int i = 0; i < out.count(); ++i)
        EXPECT_EQ((int)out.mat[i], (int)vox::VoxMat::Boat);
}

TEST(Ecosystem, DisabledEntitiesProduceNoCells) {
    auto c = eco_cfg();
    c.kelp.enabled = false; c.fish.enabled = false; c.entity.boat_enabled = false;
    vox::Ecosystem eco; eco.rebuild_if_dirty(c);
    eco.update(c, 1.0f/60.0f, 0.0f, [](float, float){ return 0.0f; });
    vox::StampList out; eco.build_stamp(c, eco_world(c), out);
    EXPECT_EQ(out.count(), 0);
}
