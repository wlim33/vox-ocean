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

// Regression guard unit test: a Water cell at the free surface (over a full
// water slab) must NOT make a same-level lateral move when the cell BELOW the
// lateral target is also Water (no genuine downhill).
//
// Block layout (index = lx + 2*ly + 4*lz):
//   0=(lx=0,ly=0,lz=0) Water   lo (below bump cell 2)
//   1=(lx=1,ly=0,lz=0) Water   dx: also below sx=3 (blocks sx guard)
//   2=(lx=0,ly=1,lz=0) Water   up: the displaced "bump" at the free surface
//   3=(lx=1,ly=1,lz=0) Water   sx: filled Water blocks the sx move outright
//   4=(lx=0,ly=0,lz=1) Water   dz: directly BELOW sz=6 (the guard target)
//   5=(lx=1,ly=0,lz=1) Water
//   6=(lx=0,ly=1,lz=1) Air     sz: the same-level z target (no genuine downhill)
//   7=(lx=1,ly=1,lz=1) Air
//
// Without the fix: up=2 tries sx=3 (Water→fail), then sz=6 (Air→succeed):
//   Water slides from mat[2] to mat[6] and the lateral cascade continues.
//   Net result: mat[3] changes Water→Air, mat[6] changes Air→Water (2 edits).
// With the fix: the sz guard checks can_sink(Water, mat[dz=4]=Water)→false,
//   blocking the slide.  Net result: nothing moves (0 edits).
TEST(MaterialCa, WaterAboveSurfaceSettlesAndSleeps) {
    using namespace vox;
    uint8_t mat[8] = {
        (uint8_t)VoxMat::Water,   // 0 lo
        (uint8_t)VoxMat::Water,   // 1 dx / below-sx
        (uint8_t)VoxMat::Water,   // 2 up = bump
        (uint8_t)VoxMat::Water,   // 3 sx (Water: blocks sx move, forcing sz check)
        (uint8_t)VoxMat::Water,   // 4 dz = below-sz (Water: no downhill → fix blocks)
        (uint8_t)VoxMat::Water,   // 5
        (uint8_t)VoxMat::Air,     // 6 sz = same-level z target
        (uint8_t)VoxMat::Air,     // 7
    };
    uint8_t before[8];
    std::copy(mat, mat+8, before);
    resolve_block(mat);
    // With the fix: nothing moves — the block must be unchanged.
    EXPECT_EQ(std::vector<uint8_t>(mat, mat+8),
              std::vector<uint8_t>(before, before+8))
        << "same-level sz move fired without genuine downhill (surface oscillation bug)";
}

// --- Bubble behavior tests (SP2-II) ---

TEST(MaterialCa, ResolveBlockBubbleRisesThroughWater) {
    using namespace vox;
    uint8_t mat[8];
    std::fill(mat, mat + 8, (uint8_t)VoxMat::Water);
    mat[0] = (uint8_t)VoxMat::Bubble;   // lower cell (ly=0) of the lx=0,lz=0 vertical pair
    resolve_block(mat);
    EXPECT_EQ(mat[2], (uint8_t)VoxMat::Bubble);  // rose to the upper cell (ly=1)
    EXPECT_EQ(mat[0], (uint8_t)VoxMat::Water);   // water sank into its place
}

TEST(MaterialCa, ResolveBlockBubbleStaysUnderAir) {
    using namespace vox;
    uint8_t mat[8];
    std::fill(mat, mat + 8, (uint8_t)VoxMat::Air);
    mat[0] = (uint8_t)VoxMat::Bubble;   // bubble below air
    resolve_block(mat);
    EXPECT_EQ(mat[0], (uint8_t)VoxMat::Bubble);  // did NOT rise into lighter air
    EXPECT_EQ(mat[2], (uint8_t)VoxMat::Air);
}

TEST(MaterialCa, BubbleRisesThroughWaterAndSleepsUnderSurface) {
    using namespace vox;
    // Representative ocean: water fills iy=0..6 across the whole extent (free surface
    // at iy=6, air at iy=7+). A bubble starts at the bottom centre. It rises straight up
    // through the water and pools just under the surface; because every surface neighbour
    // is water (not air), it cannot spill sideways. The CA must settle and sleep — the
    // SP2-I free-surface no-oscillation property applied to a gas pocket.
    MaterialCaDims d{6, 10};
    std::vector<uint8_t> g((size_t)d.extent * d.extent * d.height_cells, (uint8_t)VoxMat::Air);
    for (int iz = 0; iz < d.extent; ++iz)
        for (int ix = 0; ix < d.extent; ++ix)
            for (int iy = 0; iy <= 6; ++iy)
                g[ca_cell_index(d, ix, iy, iz)] = (uint8_t)VoxMat::Water;
    int cx = 3, cz = 3;
    g[ca_cell_index(d, cx, 0, cz)] = (uint8_t)VoxMat::Bubble;       // bubble at the bottom centre
    int water_before = count_of(g, VoxMat::Water);
    MaterialCa ca; ca.wake_box(0, 0, 0, d.extent - 1, 6, d.extent - 1);
    std::vector<uint32_t> changed;
    for (int s = 0; s < 200 && ca.awake(); ++s) { changed.clear(); ca.step(g, d, changed); }
    EXPECT_FALSE(ca.awake());                          // settled & sleeps — no free-surface oscillation
    EXPECT_EQ(count_of(g, VoxMat::Bubble), 1);         // mass conserved
    EXPECT_EQ(count_of(g, VoxMat::Water), water_before);
    // The bubble rose to the top water layer (iy=6), just beneath the air surface.
    // Scan bottom-up for the bubble's y (count_of==1 above guarantees a single cell).
    int by = -1;
    for (int iy = 0; iy < d.height_cells; ++iy)
        for (int iz = 0; iz < d.extent; ++iz)
            for (int ix = 0; ix < d.extent; ++ix)
                if (g[ca_cell_index(d, ix, iy, iz)] == (uint8_t)VoxMat::Bubble) by = iy;
    EXPECT_EQ(by, 6);
}

// Integration test: a flat-surface ocean with a patch of displaced water at the
// surface must settle and sleep.  Uses same-sized grid as World small_cfg.
// This confirms that the active-box sleeping mechanism works end-to-end for a
// realistic ocean surface scenario (complementing the resolve_block unit test).
TEST(MaterialCa, FlatOceanWithDisplacedSurfaceWaterSleeps) {
    using namespace vox;
    // 16x12x16, sea_top=8.  Full water slab y=0..7, 5x5 patch at y=8 (centre)
    // simulating 25 sand-displaced surface cells.  CA must sleep within 200 steps.
    MaterialCaDims d{16, 12};
    std::vector<uint8_t> g((size_t)d.extent*d.extent*d.height_cells, (uint8_t)VoxMat::Air);
    const int sea_top = 8;
    for (int iz = 0; iz < d.extent; ++iz)
        for (int ix = 0; ix < d.extent; ++ix)
            for (int iy = 0; iy < sea_top; ++iy)
                g[ca_cell_index(d, ix, iy, iz)] = (uint8_t)VoxMat::Water;
    const int patch_lo = 6, patch_hi = 10;
    int patch_cells = 0;
    for (int iz = patch_lo; iz <= patch_hi; ++iz)
        for (int ix = patch_lo; ix <= patch_hi; ++ix) {
            g[ca_cell_index(d, ix, sea_top, iz)] = (uint8_t)VoxMat::Water;
            ++patch_cells;
        }
    const int water_count = d.extent * d.extent * sea_top + patch_cells;

    MaterialCa ca;
    ca.wake_box(0, 0, 0, d.extent-1, d.height_cells-1, d.extent-1);

    int steps_to_sleep = -1;
    const int kBound = 200;
    for (int s = 0; s < kBound; ++s) {
        std::vector<uint32_t> changed;
        ca.step(g, d, changed);
        if (!ca.awake()) { steps_to_sleep = s + 1; break; }
    }

    EXPECT_FALSE(ca.awake()) << "CA did not sleep within " << kBound << " steps";
    SCOPED_TRACE("steps to sleep: " + std::to_string(steps_to_sleep));
    EXPECT_EQ(count_of(g, VoxMat::Water), water_count);
}

// --- SP3-I combustion_sweep micro-tests (deterministic via forced rates) ---
TEST(Combustion, FuelNextToFireIgnites) {
    using namespace vox;
    MaterialCaDims d{3, 3};
    std::vector<uint8_t> g((size_t)d.extent*d.extent*d.height_cells, (uint8_t)VoxMat::Air);
    g[ca_cell_index(d,1,1,1)] = (uint8_t)VoxMat::Fire;
    g[ca_cell_index(d,2,1,1)] = (uint8_t)VoxMat::Kelp;   // flammable (0.4)
    CombustionParams p; p.ignite_scale = 100.0f;          // force rand < flammability*scale
    std::vector<uint32_t> ch;
    combustion_sweep(g, d, /*step*/0, /*seed*/7, p, 0,0,0, 2,2,2, ch);
    EXPECT_EQ(g[ca_cell_index(d,2,1,1)], (uint8_t)VoxMat::Fire);   // kelp ignited
}

TEST(Combustion, FireBurnsOutToAsh) {
    using namespace vox;
    MaterialCaDims d{3, 3};
    std::vector<uint8_t> g((size_t)d.extent*d.extent*d.height_cells, (uint8_t)VoxMat::Air);
    g[ca_cell_index(d,1,1,1)] = (uint8_t)VoxMat::Fire;
    CombustionParams p; p.burn_out_chance = 1.0f; p.smoke_chance = 0.0f;
    std::vector<uint32_t> ch;
    combustion_sweep(g, d, 0, 7, p, 0,0,0, 2,2,2, ch);
    EXPECT_EQ(g[ca_cell_index(d,1,1,1)], (uint8_t)VoxMat::Ash);
}

TEST(Combustion, FireNextToWaterBecomesSmoke) {
    using namespace vox;
    MaterialCaDims d{3, 3};
    std::vector<uint8_t> g((size_t)d.extent*d.extent*d.height_cells, (uint8_t)VoxMat::Air);
    g[ca_cell_index(d,1,1,1)] = (uint8_t)VoxMat::Fire;
    g[ca_cell_index(d,1,0,1)] = (uint8_t)VoxMat::Water;
    CombustionParams p; p.burn_out_chance = 1.0f;        // would burn out, but water wins
    std::vector<uint32_t> ch;
    combustion_sweep(g, d, 0, 7, p, 0,0,0, 2,2,2, ch);
    EXPECT_EQ(g[ca_cell_index(d,1,1,1)], (uint8_t)VoxMat::Smoke);
}

TEST(Combustion, FireEmitsSmokeIntoAir) {
    using namespace vox;
    MaterialCaDims d{3, 3};
    std::vector<uint8_t> g((size_t)d.extent*d.extent*d.height_cells, (uint8_t)VoxMat::Air);
    g[ca_cell_index(d,1,1,1)] = (uint8_t)VoxMat::Fire;
    CombustionParams p; p.burn_out_chance = 0.0f; p.smoke_chance = 1.0f;   // emit, don't burn out
    std::vector<uint32_t> ch;
    combustion_sweep(g, d, 0, 7, p, 0,0,0, 2,2,2, ch);
    EXPECT_EQ(g[ca_cell_index(d,1,1,1)], (uint8_t)VoxMat::Fire);            // fire stays
    EXPECT_EQ(count_of(g, VoxMat::Smoke), 1);                              // one air -> smoke
}

TEST(Combustion, SmokeDissipates) {
    using namespace vox;
    MaterialCaDims d{3, 3};
    std::vector<uint8_t> g((size_t)d.extent*d.extent*d.height_cells, (uint8_t)VoxMat::Air);
    g[ca_cell_index(d,1,1,1)] = (uint8_t)VoxMat::Smoke;
    CombustionParams p; p.smoke_dissipate_chance = 1.0f;
    std::vector<uint32_t> ch;
    combustion_sweep(g, d, 0, 7, p, 0,0,0, 2,2,2, ch);
    EXPECT_EQ(g[ca_cell_index(d,1,1,1)], (uint8_t)VoxMat::Air);
}
