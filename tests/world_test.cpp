#include "world/World.h"
#include "world/EditList.h"
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

TEST(World, IngestIgnoresOutOfRangeIndex) {
    vox::World w;
    w.configure(small_cfg());
    w.begin_frame();
    vox::StampList s;
    s.push((uint32_t)w.cells().size() + 100, vox::VoxMat::Boat);   // out of range
    w.ingest(s);                                                   // must not crash/corrupt
    EXPECT_EQ(w.cells(), w.terrain_cells());                       // nothing changed
}

TEST(World, BuildEditsFirstFrameResyncs) {
    vox::World w;
    w.configure(small_cfg());
    w.begin_frame();
    vox::StampList s; s.push(5, vox::VoxMat::Kelp);
    w.ingest(s);
    vox::EditList e;
    w.build_edits(e);
    EXPECT_TRUE(e.resync);      // first frame after configure
    EXPECT_EQ(e.count(), 0);
}

TEST(World, BuildEditsDeltaBetweenFrames) {
    vox::World w;
    w.configure(small_cfg());
    // frame 0: resync
    w.begin_frame();
    vox::StampList s0; s0.push(5, vox::VoxMat::Kelp);
    w.ingest(s0);
    vox::EditList e0; w.build_edits(e0);
    ASSERT_TRUE(e0.resync);
    // frame 1: kelp moves from cell 5 to cell 6
    w.begin_frame();
    vox::StampList s1; s1.push(6, vox::VoxMat::Kelp);
    w.ingest(s1);
    vox::EditList e1; w.build_edits(e1);
    EXPECT_FALSE(e1.resync);
    // cell 5 reverts to terrain, cell 6 becomes Kelp -> exactly 2 edits
    ASSERT_EQ(e1.count(), 2);
    // find the two edits regardless of order
    bool five_reverted = false, six_kelp = false;
    for (int k = 0; k < e1.count(); ++k) {
        if (e1.idx[k] == 5u) { EXPECT_EQ(e1.mat[k], w.terrain_cells()[5]); five_reverted = true; }
        if (e1.idx[k] == 6u) { EXPECT_EQ(e1.mat[k], (uint8_t)vox::VoxMat::Kelp); six_kelp = true; }
    }
    EXPECT_TRUE(five_reverted);
    EXPECT_TRUE(six_kelp);
}

TEST(World, EditStreamReconstructsWorld) {
    vox::World w;
    w.configure(small_cfg());
    std::vector<uint8_t> replica;
    // drive several frames with different entity placements
    const uint32_t cells[] = {5, 6, 6, 100, 5};
    for (uint32_t c : cells) {
        w.begin_frame();
        vox::StampList s; s.push(c, vox::VoxMat::Fish);
        w.ingest(s);
        vox::EditList e; w.build_edits(e);
        if (e.resync) replica = w.cells();
        else vox::apply_edits(replica, e);
        EXPECT_EQ(replica, w.cells());   // stream reconstructs the world each frame
    }
}

TEST(World, ReconfigureResyncs) {
    vox::World w;
    w.configure(small_cfg(7));
    w.begin_frame(); { vox::StampList s; s.push(5, vox::VoxMat::Kelp); w.ingest(s); }
    vox::EditList e0; w.build_edits(e0);   // resync (first frame)
    w.begin_frame(); { vox::StampList s; s.push(6, vox::VoxMat::Kelp); w.ingest(s); }
    vox::EditList e1; w.build_edits(e1);   // delta
    ASSERT_FALSE(e1.resync);
    w.configure(small_cfg(8));             // terrain rebuild -> must resync next build
    w.begin_frame(); { vox::StampList s; s.push(6, vox::VoxMat::Kelp); w.ingest(s); }
    vox::EditList e2; w.build_edits(e2);
    EXPECT_TRUE(e2.resync);
}
