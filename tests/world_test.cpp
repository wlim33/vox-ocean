#include "world/World.h"
#include "world/EditList.h"
#include "voxel/VoxelWorld.h"
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
    int sea = g.water_top_cell(0.0f);   // cells filled with Water below sea level
    ASSERT_EQ((int)mat.size(), extent * extent * hc);
    for (int iz = 0; iz < extent; ++iz)
        for (int ix = 0; ix < extent; ++ix) {
            const vox::FloorColumn& fc = floor[(size_t)iz * extent + ix];
            for (int iy = 0; iy < hc; ++iy) {
                uint8_t expect;
                if (iy < fc.height)             expect = fc.material;           // terrain
                else if (iy < sea)              expect = (uint8_t)vox::VoxMat::Water; // ocean
                else                            expect = (uint8_t)vox::VoxMat::Air;
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
    int without = (int)std::count(w0.material().begin(), w0.material().end(), (uint8_t)vox::VoxMat::SandGrain);
    vox::Config c = base;
    c.sand.enabled = true; c.sand.spawn_radius = 2; c.sand.spawn_thickness = 3;
    vox::World w; w.configure(c);
    int with = (int)std::count(w.material().begin(), w.material().end(), (uint8_t)vox::VoxMat::SandGrain);
    EXPECT_GT(with, without);                             // the seed added a sand slab
    // The slab reaches the grid top at the central column; terrain never does there.
    const auto& g = w.grid();
    int extent = g.params().extent, hc = g.params().height_cells;
    EXPECT_EQ(w.material()[g.cell_index(extent / 2, hc - 1, extent / 2)], (uint8_t)vox::VoxMat::SandGrain);
}

TEST(World, StepFirstFrameResyncs) {
    vox::World w; w.configure(small_cfg());
    vox::StampList s; s.push(5, vox::VoxMat::Kelp);
    vox::EditList e; w.step(small_cfg(), 1.0f / 60, s, e);
    EXPECT_TRUE(e.resync);
    EXPECT_EQ(e.count(), 0);
}

TEST(World, StepEntityMoveRevertsAndSets) {
    vox::Config c = small_cfg();
    vox::World w; w.configure(c);
    vox::EditList e0; { vox::StampList s; s.push(5, vox::VoxMat::Kelp); w.step(c, 1.0f/60, s, e0); }
    ASSERT_TRUE(e0.resync);
    vox::EditList e1; { vox::StampList s; s.push(6, vox::VoxMat::Kelp); w.step(c, 1.0f/60, s, e1); }
    EXPECT_FALSE(e1.resync);
    ASSERT_EQ(e1.count(), 2);                       // cell 5 reverts, cell 6 -> Kelp
    bool five_reverted = false, six_kelp = false;
    for (int k = 0; k < e1.count(); ++k) {
        if (e1.idx[k] == 5u) { EXPECT_EQ(e1.mat[k], w.material()[5]); five_reverted = true; }
        if (e1.idx[k] == 6u) { EXPECT_EQ(e1.mat[k], (uint8_t)vox::VoxMat::Kelp); six_kelp = true; }
    }
    EXPECT_TRUE(five_reverted); EXPECT_TRUE(six_kelp);
}

TEST(World, StepEntityOverlayWinsOverMaterial) {
    vox::Config c = small_cfg();
    vox::World w; w.configure(c);
    vox::EditList e0; { vox::StampList s; w.step(c, 1.0f/60, s, e0); }   // resync, no entities
    vox::EditList e1; { vox::StampList s; s.push(5, vox::VoxMat::Boat); w.step(c, 1.0f/60, s, e1); }
    // cell 5 in the emitted edit must be Boat (overlay), not material_[5].
    bool found = false;
    for (int k = 0; k < e1.count(); ++k) if (e1.idx[k] == 5u) {
        EXPECT_EQ(e1.mat[k], (uint8_t)vox::VoxMat::Boat); found = true;
    }
    EXPECT_TRUE(found);
}

TEST(World, StepEditStreamReconstructsComposite) {
    vox::Config c = small_cfg();
    vox::World w; w.configure(c);
    std::vector<uint8_t> replica;
    const uint32_t cells[] = {5, 6, 6, 100, 5};
    for (uint32_t cell : cells) {
        vox::StampList s; s.push(cell, vox::VoxMat::Fish);
        vox::EditList e; w.step(c, 1.0f/60, s, e);
        if (e.resync) replica = w.materialize_composite();
        else vox::apply_edits(replica, e);
        EXPECT_EQ(replica, w.materialize_composite());
    }
}

TEST(World, SeedsWaterBelowSeaLevelAsleep) {
    using namespace vox;
    Config cfg = small_cfg();
    World w; w.configure(cfg);
    const auto& g = w.grid();
    const auto& floor = w.floor();
    const auto& cells = w.materialize_composite();   // material_ (no overlay)
    int sea = g.water_top_cell(0.0f);                // cells up to y=0
    int extent = cfg.voxel.grid_extent;
    // Find a deep-water column (floor well below sea level) that has Water from terrain top to sea,
    // and Air above sea.
    bool found_deep_water = false, air_above = true;
    for (int iz = 0; iz < extent && !found_deep_water; ++iz) {
        for (int ix = 0; ix < extent && !found_deep_water; ++ix) {
            const vox::FloorColumn& fc = floor[(size_t)iz * extent + ix];
            if (fc.height < sea) {  // terrain is below sea level
                bool any_water = false;
                for (int iy = 0; iy < cfg.voxel.height_cells; ++iy) {
                    uint8_t m = cells[ca_cell_index({extent,cfg.voxel.height_cells}, ix, iy, iz)];
                    if (iy < sea && m == (uint8_t)VoxMat::Water) any_water = true;
                    if (iy >= sea && m == (uint8_t)VoxMat::Water) air_above = false;
                }
                if (any_water && air_above) {
                    found_deep_water = true;
                }
            }
        }
    }
    EXPECT_TRUE(found_deep_water);
    EXPECT_FALSE(w.ca_awake());                       // calm ocean is asleep
}

TEST(World, BubbleSeedReplacesSubmergedWater) {
    vox::Config base = small_cfg();                       // bubble disabled
    vox::World w0; w0.configure(base);
    int without = (int)std::count(w0.material().begin(), w0.material().end(), (uint8_t)vox::VoxMat::Bubble);
    vox::Config c = small_cfg();
    c.bubble.enabled = true; c.bubble.spawn_radius = 3; c.bubble.spawn_depth = 6;  // submerged (sea cell ≈ 8)
    vox::World w; w.configure(c);
    int with = (int)std::count(w.material().begin(), w.material().end(), (uint8_t)vox::VoxMat::Bubble);
    EXPECT_EQ(without, 0);
    EXPECT_GT(with, 0);                                   // a submerged bubble blob was seeded
}

TEST(World, FireSeedIgnitesRegion) {
    vox::Config base = small_cfg();                       // fire disabled
    vox::World w0; w0.configure(base);
    int without = (int)std::count(w0.material().begin(), w0.material().end(), (uint8_t)vox::VoxMat::Fire);
    vox::Config c = small_cfg();
    c.fire.enabled = true; c.fire.spawn_radius = 3; c.fire.spawn_height = 6;
    vox::World w; w.configure(c);
    int with = (int)std::count(w.material().begin(), w.material().end(), (uint8_t)vox::VoxMat::Fire);
    EXPECT_EQ(without, 0);
    EXPECT_GT(with, 0);                                   // an ignition region was seeded
}

TEST(World, LavaSeedPlacesLava) {
    vox::Config c = small_cfg();
    c.lava.enabled = true; c.lava.spawn_radius = 3; c.lava.spawn_height = 6;
    vox::World w; w.configure(c);
    int with = (int)std::count(w.material().begin(), w.material().end(), (uint8_t)vox::VoxMat::Lava);
    EXPECT_GT(with, 0);
}

TEST(World, StepSettledSandEmitsNoEditsAfterSleep) {
    vox::Config c = small_cfg();
    c.sand.enabled = true; c.sand.spawn_radius = 2; c.sand.spawn_thickness = 2;
    // Normal (realistic) grid: sea level is inside the grid (partial ocean).
    // Sand sinks through water; the displaced water rises to the free surface.
    // The CA must settle and sleep — the displaced-water oscillation bug must not
    // keep the CA awake indefinitely.
    vox::World w; w.configure(c);
    vox::StampList empty;
    vox::EditList e; w.step(c, 1.0f/60, empty, e);       // frame 0: resync
    ASSERT_TRUE(e.resync);
    // Run until the sand settles (bounded), then assert a quiet frame emits nothing.
    // 2000 frames: sand sinks through the deep water column and displaced water must re-level before the CA sleeps.
    for (int i = 0; i < 2000; ++i) { vox::EditList ei; w.step(c, 1.0f/60, empty, ei); if (ei.count() == 0) break; }
    vox::EditList quiet; w.step(c, 1.0f/60, empty, quiet);
    EXPECT_EQ(quiet.count(), 0);
}
