#include "world/World.h"
#include "world/EditList.h"
#include "voxel/VoxelWorld.h"
#include "voxel/MaterialCa.h"
#include "entity/StampBudget.h"
#include "core/Config.h"
#include <gtest/gtest.h>
#include <algorithm>

static vox::Config small_cfg(int seed = 7) {
    vox::Config c;
    c.voxel.grid_extent = 16; c.voxel.height_cells = 16;
    c.voxel.voxel_size_m = 0.5f; c.voxel.height_step_m = 0.25f;
    c.voxel.base_depth_m = 2.0f; c.voxel.floor_seed = seed;
    c.kelp.enabled = false; c.fish.enabled = false; c.entity.boat_enabled = false;
    c.sand.enabled = false;
    return c;
}

TEST(World, MaterialSeededFromFloor) {
    vox::World w; w.configure(small_cfg());
    ASSERT_TRUE(w.configured());
    const auto& floor = w.floor();
    const auto& mat = w.material();
    const auto& g = w.grid();
    int extent = g.params().extent, hc = g.params().height_cells;
    ASSERT_EQ((int)mat.size(), extent * extent * hc);
    for (int iz = 0; iz < extent; ++iz)
        for (int ix = 0; ix < extent; ++ix) {
            const vox::FloorColumn& fc = floor[(size_t)iz * extent + ix];
            for (int iy = 0; iy < hc; ++iy) {
                uint8_t expect = (iy < fc.height) ? fc.material : (uint8_t)vox::VoxMat::Air;
                EXPECT_EQ(mat[g.cell_index(ix, iy, iz)], expect);
            }
        }
}

TEST(World, CompositeEqualsMaterialWithNoOverlay) {
    vox::World w; w.configure(small_cfg());
    EXPECT_EQ(w.materialize_composite(), w.material());   // no overlay applied yet
}

TEST(World, FloorTopYMatchesQuantization) {
    vox::World w; w.configure(small_cfg());
    const auto& g = w.grid();
    int extent = g.params().extent;
    float half = 0.5f * extent * g.params().voxel_size_m;
    int ix = (int)((0.0f + half) / g.params().voxel_size_m);
    int iz = (int)((0.0f + half) / g.params().voxel_size_m);
    int h = w.floor()[(size_t)iz * extent + ix].height;
    float expect = -g.params().base_depth_m + (float)h * g.params().height_step_m;
    EXPECT_FLOAT_EQ(w.floor_top_y(0.0f, 0.0f), expect);
}

TEST(World, ReconfigureRegeneratesOnSeedChange) {
    vox::World a; a.configure(small_cfg(7));
    vox::World b; b.configure(small_cfg(8));
    EXPECT_NE(a.material(), b.material());
}

TEST(World, ConfigureIdempotent) {
    vox::World w; w.configure(small_cfg(7));
    auto snapshot = w.material();
    w.configure(small_cfg(7));
    EXPECT_EQ(w.material(), snapshot);
}

TEST(World, SandSeedAddsSandAboveTerrain) {
    vox::Config base = small_cfg();                       // sand disabled
    vox::World w0; w0.configure(base);
    int without = (int)std::count(w0.material().begin(), w0.material().end(), (uint8_t)vox::VoxMat::Sand);
    vox::Config c = base;
    c.sand.enabled = true; c.sand.spawn_radius = 2; c.sand.spawn_thickness = 3;
    vox::World w; w.configure(c);
    int with = (int)std::count(w.material().begin(), w.material().end(), (uint8_t)vox::VoxMat::Sand);
    EXPECT_GT(with, without);                             // the seed added a sand slab
    // The slab reaches the grid top at the central column; terrain never does there.
    const auto& g = w.grid();
    int extent = g.params().extent, hc = g.params().height_cells;
    EXPECT_EQ(w.material()[g.cell_index(extent / 2, hc - 1, extent / 2)], (uint8_t)vox::VoxMat::Sand);
}
