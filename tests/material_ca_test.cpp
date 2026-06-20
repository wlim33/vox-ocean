#include "voxel/MaterialCa.h"
#include "voxel/VoxelWorld.h"
#include <gtest/gtest.h>

using namespace vox;

static int count_of(const std::vector<uint8_t>& g, VoxMat m) {
    return (int)std::count(g.begin(), g.end(), (uint8_t)m);
}

TEST(MaterialCa, IndexMatchesVoxelWorld) {
    MaterialCaDims d{16, 12};
    VoxelWorld vw(VoxelWorldParams{16, 12, 0.5f, 0.25f, 2.0f});
    for (int iz : {0, 3, 15}) for (int iy : {0, 5, 11}) for (int ix : {0, 7, 15})
        EXPECT_EQ(ca_cell_index(d, ix, iy, iz), vw.cell_index(ix, iy, iz));
}

// --- resolve_block micro-tests (id-based, SP2-I) ---

TEST(MaterialCa, ResolveBlockSandGrainSinksIntoAir) {
    // SandGrain (density 1600) is denser than Air (density 1.2) -> SandGrain sinks.
    // Block: upper cell = SandGrain (index 2 = lx0,ly1,lz0), lower = Air (index 0).
    uint8_t mat[8];
    for (auto& m : mat) m = (uint8_t)VoxMat::Air;
    mat[2] = (uint8_t)VoxMat::SandGrain;  // upper
    resolve_block(mat);
    EXPECT_EQ(mat[0], (uint8_t)VoxMat::SandGrain);  // sank to lower
    EXPECT_EQ(mat[2], (uint8_t)VoxMat::Air);         // vacated upper
}

TEST(MaterialCa, ResolveBlockConservesAllMaterials) {
    // Fill a block with a mix; resolve must be a permutation (all ids preserved).
    uint8_t mat[8] = {
        (uint8_t)VoxMat::Rock,      (uint8_t)VoxMat::SandGrain,
        (uint8_t)VoxMat::SandGrain, (uint8_t)VoxMat::Air,
        (uint8_t)VoxMat::Air,       (uint8_t)VoxMat::SandGrain,
        (uint8_t)VoxMat::Air,       (uint8_t)VoxMat::SandGrain
    };
    int sand_before = count_of(std::vector<uint8_t>(mat, mat+8), VoxMat::SandGrain);
    int rock_before = count_of(std::vector<uint8_t>(mat, mat+8), VoxMat::Rock);
    int air_before  = count_of(std::vector<uint8_t>(mat, mat+8), VoxMat::Air);
    resolve_block(mat);
    EXPECT_EQ(count_of(std::vector<uint8_t>(mat, mat+8), VoxMat::SandGrain), sand_before);
    EXPECT_EQ(count_of(std::vector<uint8_t>(mat, mat+8), VoxMat::Rock),      rock_before);
    EXPECT_EQ(count_of(std::vector<uint8_t>(mat, mat+8), VoxMat::Air),       air_before);
}

TEST(MaterialCa, ResolveBlockRockPinned) {
    // Rock is movable=false; it must not move even if Air is below it.
    uint8_t mat[8];
    for (auto& m : mat) m = (uint8_t)VoxMat::Air;
    mat[2] = (uint8_t)VoxMat::Rock;  // upper cell
    resolve_block(mat);
    EXPECT_EQ(mat[2], (uint8_t)VoxMat::Rock);   // pinned — Rock does not fall
    EXPECT_EQ(mat[0], (uint8_t)VoxMat::Air);    // Air below unchanged
}

TEST(MaterialCa, ResolveBlockWaterSinksIntoAir) {
    // Water (density 1000) > Air (density 1.2): Water sinks.
    uint8_t mat[8];
    for (auto& m : mat) m = (uint8_t)VoxMat::Air;
    mat[2] = (uint8_t)VoxMat::Water;  // upper cell
    resolve_block(mat);
    EXPECT_EQ(mat[0], (uint8_t)VoxMat::Water);
    EXPECT_EQ(mat[2], (uint8_t)VoxMat::Air);
}

// --- Sweep / step integration tests ---

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
    EXPECT_EQ(count_of(g, VoxMat::SandGrain), 1);
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
    EXPECT_EQ(count_of(g, VoxMat::SandGrain), 1);
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
    EXPECT_EQ(count_of(g, VoxMat::SandGrain), 1);
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

// --- New density/fluidity behavior tests (SP2-I) ---

TEST(MaterialCa, SandSinksThroughWater) {
    using namespace vox;
    MaterialCaDims d{4, 12};
    std::vector<uint8_t> g((size_t)d.extent*d.extent*d.height_cells, (uint8_t)VoxMat::Air);
    int x=1, z=1;
    for (int iy=0; iy<6; ++iy) g[ca_cell_index(d,x,iy,z)] = (uint8_t)VoxMat::Water; // water column 0..5
    g[ca_cell_index(d,x,6,z)] = (uint8_t)VoxMat::SandGrain;                          // grain on top
    MaterialCa ca; ca.wake_box(x,0,z,x,6,z);
    std::vector<uint32_t> changed;
    for (int s=0; s<60 && ca.awake(); ++s){ changed.clear(); ca.step(g,d,changed); }
    EXPECT_EQ(g[ca_cell_index(d,x,0,z)], (uint8_t)VoxMat::SandGrain);  // grain reached the floor
    EXPECT_EQ(count_of(g, VoxMat::SandGrain), 1);                      // mass conserved
    EXPECT_EQ(count_of(g, VoxMat::Water), 6);                         // water conserved (displaced up)
}

TEST(MaterialCa, WaterLevels) {
    using namespace vox;
    MaterialCaDims d{6, 8};
    std::vector<uint8_t> g((size_t)d.extent*d.extent*d.height_cells, (uint8_t)VoxMat::Air);
    // A tall thin water column should spread to a shorter, wider puddle.
    for (int iy=0; iy<6; ++iy) g[ca_cell_index(d,2,iy,2)] = (uint8_t)VoxMat::Water;
    MaterialCa ca; ca.wake_box(0,0,0,5,6,5);
    std::vector<uint32_t> changed;
    for (int s=0; s<80 && ca.awake(); ++s){ changed.clear(); ca.step(g,d,changed); }
    EXPECT_EQ(count_of(g, VoxMat::Water), 6);                         // conserved
    // No single column still holds the full 6-tall stack (it spread out).
    int tallest=0; for (int z=0;z<6;++z) for (int x=0;x<6;++x){int h=0;
        for(int iy=0;iy<8;++iy) if(g[ca_cell_index(d,x,iy,z)]==(uint8_t)VoxMat::Water) h++; tallest=std::max(tallest,h);}
    EXPECT_LT(tallest, 6);
}

TEST(MaterialCa, SolidPinnedAndSettledSleeps) {
    using namespace vox;
    MaterialCaDims d{4, 8};
    std::vector<uint8_t> g((size_t)d.extent*d.extent*d.height_cells, (uint8_t)VoxMat::Air);
    g[ca_cell_index(d,1,3,1)] = (uint8_t)VoxMat::Rock;       // floating rock must NOT move
    MaterialCa ca; ca.wake_box(0,0,0,3,7,3);
    std::vector<uint32_t> changed;
    for (int s=0; s<10 && ca.awake(); ++s){ changed.clear(); ca.step(g,d,changed); }
    EXPECT_EQ(g[ca_cell_index(d,1,3,1)], (uint8_t)VoxMat::Rock);  // pinned
    EXPECT_FALSE(ca.awake());                                     // nothing moved -> sleeps
}

TEST(MaterialCa, Determinism) {
    // Two independent runs produce identical final grids.
    using namespace vox;
    MaterialCaDims d{6, 10};
    auto make_grid = [&]() {
        std::vector<uint8_t> g((size_t)d.extent*d.extent*d.height_cells, (uint8_t)VoxMat::Air);
        // Mixed scenario: water column + sand grain on top.
        for (int iy=0; iy<4; ++iy) g[ca_cell_index(d,3,iy,3)] = (uint8_t)VoxMat::Water;
        g[ca_cell_index(d,3,4,3)] = (uint8_t)VoxMat::SandGrain;
        MaterialCa ca; ca.wake_box(0,0,0,5,5,5);
        std::vector<uint32_t> changed;
        for (int s=0; s<80 && ca.awake(); ++s){ changed.clear(); ca.step(g,d,changed); }
        return g;
    };
    EXPECT_EQ(make_grid(), make_grid());
}
