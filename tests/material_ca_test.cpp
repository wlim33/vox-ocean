#include "voxel/MaterialCa.h"
#include "voxel/VoxelWorld.h"
#include <gtest/gtest.h>
#include <numeric>

using namespace vox;

static int sand_count(const std::vector<uint8_t>& g) {
    return (int)std::count(g.begin(), g.end(), (uint8_t)VoxMat::SandGrain);
}

TEST(MaterialCa, IndexMatchesVoxelWorld) {
    MaterialCaDims d{16, 12};
    VoxelWorld vw(VoxelWorldParams{16, 12, 0.5f, 0.25f, 2.0f});
    for (int iz : {0, 3, 15}) for (int iy : {0, 5, 11}) for (int ix : {0, 7, 15})
        EXPECT_EQ(ca_cell_index(d, ix, iy, iz), vw.cell_index(ix, iy, iz));
}

TEST(MaterialCa, ResolveBlockSandFallsToFloorOfBlock) {
    // upper cell sand, lower empty -> sand drops to lower.
    uint8_t cls[8] = {CA_EMPTY, CA_EMPTY, CA_SAND, CA_EMPTY,
                      CA_EMPTY, CA_EMPTY, CA_EMPTY, CA_EMPTY};
    // index 2 = (lx0,ly1,lz0) upper; index 0 = (lx0,ly0,lz0) lower.
    resolve_block(cls);
    EXPECT_EQ(cls[0], CA_SAND);
    EXPECT_EQ(cls[2], CA_EMPTY);
}

TEST(MaterialCa, ResolveBlockConservesSand) {
    uint8_t cls[8] = {CA_BARRIER, CA_SAND, CA_SAND, CA_EMPTY,
                      CA_EMPTY,   CA_SAND, CA_EMPTY, CA_SAND};
    int before = 0; for (uint8_t c : cls) before += (c == CA_SAND);
    resolve_block(cls);
    int after = 0; for (uint8_t c : cls) after += (c == CA_SAND);
    EXPECT_EQ(before, after);
}

TEST(MaterialCa, SweepDropsOneGrainOneCell) {
    MaterialCaDims d{4, 8};
    std::vector<uint8_t> g((size_t)d.extent * d.extent * d.height_cells, (uint8_t)VoxMat::Air);
    int ix = 1, iz = 1, iy = 5;
    g[ca_cell_index(d, ix, iy, iz)] = (uint8_t)VoxMat::SandGrain;
    std::vector<uint32_t> changed;
    // oy=0 puts y=4,5 in one block -> grain at 5 falls to 4.
    margolus_sweep(g, d, 0, 0, 0, 0, 0, 0, d.extent - 1, d.height_cells - 1, d.extent - 1, changed);
    EXPECT_EQ(g[ca_cell_index(d, ix, 5, iz)], (uint8_t)VoxMat::Air);
    EXPECT_EQ(g[ca_cell_index(d, ix, 4, iz)], (uint8_t)VoxMat::SandGrain);
    EXPECT_EQ(sand_count(g), 1);
}

TEST(MaterialCa, SweepLeavesTerrainSandInPlace) {
    MaterialCaDims d{4, 8};
    std::vector<uint8_t> g((size_t)d.extent * d.extent * d.height_cells, (uint8_t)VoxMat::Air);
    // column (1,1): terrain up to y<3 made of Rock (Solid); air above.
    for (int iy = 0; iy < 3; ++iy) g[ca_cell_index(d, 1, iy, 1)] = (uint8_t)VoxMat::Rock;
    std::vector<uint32_t> changed;
    for (int s = 0; s < 4; ++s)
        margolus_sweep(g, d, 0, s % 2, 0, 0, 0, 0, d.extent-1, d.height_cells-1, d.extent-1, changed);
    for (int iy = 0; iy < 3; ++iy)
        EXPECT_EQ(g[ca_cell_index(d, 1, iy, 1)], (uint8_t)VoxMat::Rock);  // terrain unmoved
    EXPECT_TRUE(changed.empty());
}

TEST(MaterialCa, GrainFallsToFloorOverSteps) {
    MaterialCaDims d{4, 16};
    std::vector<uint8_t> g((size_t)d.extent * d.extent * d.height_cells, (uint8_t)VoxMat::Air);
    g[ca_cell_index(d, 2, 12, 2)] = (uint8_t)VoxMat::SandGrain;
    MaterialCa ca;
    ca.wake_box(2, 12, 2, 2, 12, 2);
    for (int s = 0; s < 40 && ca.awake(); ++s) {
        std::vector<uint32_t> changed;
        ca.step(g, d, changed);
    }
    EXPECT_EQ(g[ca_cell_index(d, 2, 0, 2)], (uint8_t)VoxMat::SandGrain);  // landed on the floor
    EXPECT_EQ(sand_count(g), 1);
    EXPECT_FALSE(ca.awake());                                        // settled -> asleep
}

TEST(MaterialCa, SettledSceneSleeps) {
    MaterialCaDims d{4, 8};
    std::vector<uint8_t> g((size_t)d.extent * d.extent * d.height_cells, (uint8_t)VoxMat::Air);
    g[ca_cell_index(d, 2, 0, 2)] = (uint8_t)VoxMat::SandGrain;    // already on the floor
    MaterialCa ca;
    ca.wake_box(2, 0, 2, 2, 0, 2);
    bool moved = false;
    for (int s = 0; s < 8; ++s) {                                   // > one full phase cycle
        std::vector<uint32_t> changed;
        ca.step(g, d, changed);
        if (!changed.empty()) moved = true;
    }
    EXPECT_FALSE(moved);                                            // a grain on the floor never moves
    EXPECT_FALSE(ca.awake());                                       // CA sleeps after a quiet cycle
    EXPECT_EQ(sand_count(g), 1);
}

TEST(MaterialCa, FallingGrainKeepsSandGrainIdentity) {
    MaterialCaDims d{6, 16};
    std::vector<uint8_t> g((size_t)d.extent * d.extent * d.height_cells, (uint8_t)VoxMat::Air);
    g[ca_cell_index(d, 2, 12, 2)] = (uint8_t)VoxMat::SandGrain;
    MaterialCa ca;
    ca.wake_box(2, 12, 2, 2, 12, 2);
    std::vector<uint32_t> changed;
    for (int s = 0; s < 40 && ca.awake(); ++s) { changed.clear(); ca.step(g, d, changed); }
    // Landed grain is still SandGrain, never rewritten to Sand; count conserved.
    EXPECT_EQ(g[ca_cell_index(d, 2, 0, 2)], (uint8_t)VoxMat::SandGrain);
    EXPECT_EQ(std::count(g.begin(), g.end(), (uint8_t)VoxMat::SandGrain), 1);
    EXPECT_EQ(std::count(g.begin(), g.end(), (uint8_t)VoxMat::Sand), 0);
}
