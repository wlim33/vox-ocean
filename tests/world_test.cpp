#include "world/World.h"
#include "voxel/VoxelWorld.h"
#include "entity/StampBudget.h"
#include "core/Config.h"
#include <gtest/gtest.h>

static vox::Config small_cfg(int seed = 7) {
    vox::Config c;
    c.voxel.grid_extent  = 16;
    c.voxel.height_cells = 16;
    c.voxel.voxel_size_m = 0.5f;
    c.voxel.height_step_m = 0.25f;
    c.voxel.base_depth_m  = 2.0f;   // 8 steps, integer multiple of height_step
    c.voxel.floor_seed    = seed;
    return c;
}

TEST(World, TerrainBakeMatchesFloorLayout) {
    vox::World w;
    w.configure(small_cfg());
    ASSERT_TRUE(w.configured());
    const auto& floor = w.floor();
    const auto& terr  = w.terrain_cells();
    const auto& g     = w.grid();
    int extent = g.params().extent, hc = g.params().height_cells;
    ASSERT_EQ((int)terr.size(), extent * extent * hc);
    for (int iz = 0; iz < extent; ++iz)
        for (int ix = 0; ix < extent; ++ix) {
            const vox::FloorColumn& fc = floor[(size_t)iz * extent + ix];
            for (int iy = 0; iy < hc; ++iy) {
                uint8_t expect = (iy < fc.height) ? fc.material : (uint8_t)vox::VoxMat::Air;
                EXPECT_EQ(terr[g.cell_index(ix, iy, iz)], expect);
            }
        }
}

TEST(World, FloorTopYMatchesQuantization) {
    vox::World w;
    w.configure(small_cfg());
    const auto& g = w.grid();
    int extent = g.params().extent;
    float half = 0.5f * extent * g.params().voxel_size_m;
    // sample the center column
    float x = 0.0f, z = 0.0f;
    int ix = (int)((x + half) / g.params().voxel_size_m);
    int iz = (int)((z + half) / g.params().voxel_size_m);
    int h = w.floor()[(size_t)iz * extent + ix].height;
    float expect = -g.params().base_depth_m + (float)h * g.params().height_step_m;
    EXPECT_FLOAT_EQ(w.floor_top_y(x, z), expect);
}

TEST(World, BeginFrameResetsToTerrain) {
    vox::World w;
    w.configure(small_cfg());
    vox::StampList s;
    s.push(0, vox::VoxMat::Kelp);
    w.begin_frame();
    w.ingest(s);
    EXPECT_NE(w.cells()[0], w.terrain_cells()[0]);   // overlay applied
    w.begin_frame();
    EXPECT_EQ(w.cells(), w.terrain_cells());          // overlay cleared
}

TEST(World, IngestCompositesLastWriterWins) {
    vox::World w;
    w.configure(small_cfg());
    w.begin_frame();
    vox::StampList s;
    s.push(5, vox::VoxMat::Kelp);   // kelp first
    s.push(5, vox::VoxMat::Boat);   // boat wins (appended last)
    w.ingest(s);
    EXPECT_EQ(w.cells()[5], (uint8_t)vox::VoxMat::Boat);
}

TEST(World, ReconfigureRegeneratesOnSeedChange) {
    vox::World a; a.configure(small_cfg(7));
    vox::World b; b.configure(small_cfg(8));
    EXPECT_NE(a.terrain_cells(), b.terrain_cells());   // different seed -> different floor
}

TEST(World, ConfigureIdempotent) {
    vox::World w;
    w.configure(small_cfg(7));
    auto snapshot = w.terrain_cells();
    w.configure(small_cfg(7));   // same params -> no change
    EXPECT_EQ(w.terrain_cells(), snapshot);
}
