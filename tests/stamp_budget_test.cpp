#include "entity/StampBudget.h"
#include "core/Config.h"
#include <gtest/gtest.h>

TEST(StampBudget, KelpStalkCountFollowsDensity) {
    vox::Config c;
    c.voxel.grid_extent = 100;        // 10000 columns
    c.kelp.enabled = true;
    c.kelp.density = 0.03f;
    EXPECT_EQ(vox::kelp_stalk_count(c), 300);
    c.kelp.enabled = false;
    EXPECT_EQ(vox::kelp_stalk_count(c), 0);
}

TEST(StampBudget, KelpStalkCountIsCappedAtScale) {
    vox::Config c;
    c.kelp.enabled = true;
    c.kelp.density = 0.02f;
    c.kelp.max_stalks = 8192;            // default cap
    // Below the cap: still scales as density * extent^2.
    c.voxel.grid_extent = 192;           // round(0.02 * 36864) = 737
    EXPECT_EQ(vox::kelp_stalk_count(c), 737);
    // Past the cap: bounded — no more quadratic per-frame stamping blowup.
    c.voxel.grid_extent = 1024;          // round(0.02 * 1048576) = 20972 -> capped
    EXPECT_EQ(vox::kelp_stalk_count(c), 8192);
    // max_stalks = 0 is the unlimited escape hatch.
    c.kelp.max_stalks = 0;
    EXPECT_EQ(vox::kelp_stalk_count(c), 20972);
}

TEST(StampBudget, MaxStampCellsBoundsAllEntities) {
    vox::Config c;
    c.voxel.grid_extent = 100;
    c.voxel.height_step_m = 0.25f;
    c.kelp.enabled = true; c.kelp.density = 0.03f; c.kelp.max_height_m = 5.0f;
    c.fish.enabled = true; c.fish.school_count = 4; c.fish.per_school = 25;
    int expect = vox::kelp_stalk_count(c) * vox::kelp_cells_per_stalk(c)
               + 4 * 25 * vox::FISH_CELLS
               + vox::boat_max_cells(c);
    EXPECT_EQ(vox::max_stamp_cells(c), expect);
    EXPECT_GT(vox::max_stamp_cells(c), 0);
}

TEST(StampList, PushAndPriorityOrder) {
    vox::StampList s;
    s.push(10, vox::VoxMat::Kelp);
    s.push(20, vox::VoxMat::Fish);
    s.push(10, vox::VoxMat::Boat);   // same cell as kelp; boat pushed last
    EXPECT_EQ(s.count(), 3);
    int last_for_10 = -1;
    for (int i = 0; i < s.count(); ++i)
        if (s.idx[i] == 10u) last_for_10 = (int)s.mat[i];
    EXPECT_EQ(last_for_10, (int)vox::VoxMat::Boat);   // last writer wins
    s.clear();
    EXPECT_EQ(s.count(), 0);
}
