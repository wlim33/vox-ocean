#include "entity/Fish.h"
#include "entity/StampBudget.h"
#include "voxel/VoxelWorld.h"
#include "core/Config.h"
#include <gtest/gtest.h>
#include <cmath>

static vox::Config fish_cfg() {
    vox::Config c;
    c.voxel.grid_extent = 64; c.voxel.height_cells = 64;
    c.voxel.voxel_size_m = 0.5f; c.voxel.height_step_m = 0.25f; c.voxel.base_depth_m = 10.0f;
    c.fish.enabled = true; c.fish.school_count = 3; c.fish.per_school = 10;
    c.fish.speed_mps = 2.0f; c.fish.depth_frac = 0.5f; c.fish.spread_m = 3.0f; c.fish.seed = 202;
    return c;
}
static float surf0(float, float)   { return 0.0f; }    // surface at y=0
static float floorm6(float, float) { return -6.0f; }   // floor top at y=-6

TEST(Fish, RebuildIsDeterministicAndCounts) {
    auto c = fish_cfg();
    vox::FishSchools a, b;
    a.rebuild(c); b.rebuild(c);
    EXPECT_EQ(a.fish().size(), (size_t)(c.fish.school_count * c.fish.per_school));
    EXPECT_EQ(a.fish().size(), b.fish().size());
}

TEST(Fish, UpdateIsDeterministic) {
    auto c = fish_cfg();
    vox::FishSchools a, b;
    a.rebuild(c); b.rebuild(c);
    for (int i = 0; i < 200; ++i) {
        float t = i / 60.0f;
        a.update(c, 1.0f/60.0f, t, surf0, floorm6);
        b.update(c, 1.0f/60.0f, t, surf0, floorm6);
    }
    ASSERT_EQ(a.fish().size(), b.fish().size());
    for (size_t i = 0; i < a.fish().size(); ++i) {
        EXPECT_FLOAT_EQ(a.fish()[i].pos.x, b.fish()[i].pos.x);
        EXPECT_FLOAT_EQ(a.fish()[i].pos.y, b.fish()[i].pos.y);
        EXPECT_FLOAT_EQ(a.fish()[i].pos.z, b.fish()[i].pos.z);
    }
}

TEST(Fish, StaysInTheDioramaEnvelope) {
    auto c = fish_cfg();
    vox::FishSchools sch; sch.rebuild(c);
    for (int i = 0; i < 400; ++i)
        sch.update(c, 1.0f/60.0f, i/60.0f, surf0, floorm6);
    // Centroids wander inside the patch; members trail within spread_m. Bound
    // = half-extent + spread + a voxel of slack.
    float half = 0.5f * c.voxel.grid_extent * c.voxel.voxel_size_m;
    float bound = half + c.fish.spread_m + 1.0f;
    for (const auto& f : sch.fish()) {
        EXPECT_LT(std::abs(f.pos.x), bound);
        EXPECT_LT(std::abs(f.pos.z), bound);
    }
}

TEST(Fish, HoldsTheMidwaterDepthBand) {
    auto c = fish_cfg();   // depth_frac 0.5, floor -6, surface 0
    vox::FishSchools sch; sch.rebuild(c);
    for (int i = 0; i < 200; ++i)
        sch.update(c, 1.0f/60.0f, i/60.0f, surf0, floorm6);
    for (const auto& f : sch.fish()) {
        EXPECT_GT(f.pos.y, -6.0f + 0.4f);   // above the floor margin
        EXPECT_LT(f.pos.y,  0.0f - 0.4f);   // below the surface margin
    }
}

TEST(Fish, StampMarksFishVoxels) {
    auto c = fish_cfg();
    vox::FishSchools sch; sch.rebuild(c);
    sch.update(c, 1.0f/60.0f, 0.0f, surf0, floorm6);
    vox::VoxelWorld w({c.voxel.grid_extent, c.voxel.height_cells, c.voxel.voxel_size_m,
                       c.voxel.height_step_m, c.voxel.base_depth_m});
    vox::StampList out; sch.build_stamp(c, w, out);
    EXPECT_EQ(out.count(), (int)sch.fish().size() * vox::FISH_CELLS);
    for (int i = 0; i < out.count(); ++i)
        EXPECT_EQ((int)out.mat[i], (int)vox::VoxMat::Fish);
}
