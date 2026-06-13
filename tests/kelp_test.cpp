#include "entity/Kelp.h"
#include "entity/StampBudget.h"
#include "voxel/VoxelWorld.h"
#include "voxel/FloorGen.h"
#include "core/Config.h"
#include <gtest/gtest.h>

static vox::Config kelp_cfg() {
    vox::Config c;
    c.voxel.grid_extent = 64; c.voxel.height_cells = 64;
    c.voxel.voxel_size_m = 0.5f; c.voxel.height_step_m = 0.25f; c.voxel.base_depth_m = 10.0f;
    c.kelp.enabled = true; c.kelp.density = 0.05f; c.kelp.max_height_m = 6.0f;
    c.kelp.sway_strength = 1.0f; c.kelp.sway_ambient = 0.0f; c.kelp.seed = 101;
    return c;
}
// Flat floor: every column's terrain top at cell 4.
static std::vector<vox::FloorColumn> flat_floor(const vox::Config& c) {
    return std::vector<vox::FloorColumn>(
        (size_t)c.voxel.grid_extent * c.voxel.grid_extent,
        { (uint8_t)4, (uint8_t)vox::VoxMat::Sand });
}

TEST(Kelp, PlacementIsSeededAndDeterministic) {
    auto c = kelp_cfg(); auto floor = flat_floor(c);
    vox::KelpBed a, b;
    a.rebuild(c, floor); b.rebuild(c, floor);
    ASSERT_EQ(a.stalks().size(), b.stalks().size());
    EXPECT_GT(a.stalks().size(), 0u);
    for (size_t i = 0; i < a.stalks().size(); ++i) {
        EXPECT_EQ(a.stalks()[i].ix, b.stalks()[i].ix);
        EXPECT_EQ(a.stalks()[i].iz, b.stalks()[i].iz);
    }
    EXPECT_LE((int)a.stalks().size(), vox::kelp_stalk_count(c));   // within budget
}

TEST(Kelp, DensityScalesStalkCount) {
    auto lo = kelp_cfg(); lo.kelp.density = 0.02f;
    auto hi = kelp_cfg(); hi.kelp.density = 0.10f;
    vox::KelpBed a, b;
    a.rebuild(lo, flat_floor(lo)); b.rebuild(hi, flat_floor(hi));
    EXPECT_LT(a.stalks().size(), b.stalks().size());
}

TEST(Kelp, AnchorsRootOnTheFloor) {
    auto c = kelp_cfg(); auto floor = flat_floor(c);
    vox::KelpBed bed; bed.rebuild(c, floor);
    for (const auto& s : bed.stalks()) {
        EXPECT_GE(s.ix, 0); EXPECT_LT(s.ix, c.voxel.grid_extent);
        EXPECT_GE(s.iz, 0); EXPECT_LT(s.iz, c.voxel.grid_extent);
        EXPECT_EQ(s.base_cell, 4);              // flat floor top
        EXPECT_GE(s.height_cells, 1);
    }
}

TEST(Kelp, UprightWhenBecalmedAndNoAmbient) {
    auto c = kelp_cfg();                          // sway_ambient = 0
    vox::KelpBed bed; bed.rebuild(c, flat_floor(c));
    bed.update(c, 0.0f, [](float, float){ return 0.0f; });   // flat water -> zero gradient
    vox::VoxelWorld w({c.voxel.grid_extent, c.voxel.height_cells, c.voxel.voxel_size_m,
                       c.voxel.height_step_m, c.voxel.base_depth_m});
    vox::StampList out; bed.build_stamp(c, w, out);
    ASSERT_GT(out.count(), 0);
    for (int i = 0; i < out.count(); ++i) {
        int ix, iy, iz; w.decode_cell_index((int)out.idx[i], ix, iy, iz);
        bool matched = false;
        for (const auto& s : bed.stalks())
            if (s.ix == ix && s.iz == iz) { matched = true; break; }
        EXPECT_TRUE(matched);                      // no lean -> stalk stays in its anchor column
        EXPECT_EQ((int)out.mat[i], (int)vox::VoxMat::Kelp);
    }
}

TEST(Kelp, LeansAlongTheWaterGradient) {
    auto c = kelp_cfg();
    vox::KelpBed bed; bed.rebuild(c, flat_floor(c));
    bed.update(c, 0.0f, [](float x, float){ return x; });   // water rising toward +x
    ASSERT_GT(bed.stalks().size(), 0u);
    for (const auto& s : bed.stalks()) {
        EXPECT_GT(s.lean.x, 0.0f);
        EXPECT_NEAR(s.lean.y, 0.0f, 1e-3f);
    }
}
