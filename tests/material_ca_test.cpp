#include "voxel/MaterialCa.h"
#include "voxel/MaterialRegistry.h"   // material_props (oracle reads flammability)
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

TEST(Combustion, WaterNextToFireBoilsToSteam) {
    using namespace vox;
    MaterialCaDims d{3, 3};
    std::vector<uint8_t> g((size_t)d.extent*d.extent*d.height_cells, (uint8_t)VoxMat::Air);
    g[ca_cell_index(d,1,1,1)] = (uint8_t)VoxMat::Water;
    g[ca_cell_index(d,2,1,1)] = (uint8_t)VoxMat::Fire;
    CombustionParams p; p.boil_chance = 1.0f;
    std::vector<uint32_t> ch;
    combustion_sweep(g, d, 0, 7, p, 0,0,0, 2,2,2, ch);
    EXPECT_EQ(g[ca_cell_index(d,1,1,1)], (uint8_t)VoxMat::Steam);   // water boiled
}

TEST(Combustion, SteamWithoutFireCondensesToWater) {
    using namespace vox;
    MaterialCaDims d{3, 3};
    std::vector<uint8_t> g((size_t)d.extent*d.extent*d.height_cells, (uint8_t)VoxMat::Air);
    g[ca_cell_index(d,1,1,1)] = (uint8_t)VoxMat::Steam;
    CombustionParams p; p.condense_chance = 1.0f;
    std::vector<uint32_t> ch;
    combustion_sweep(g, d, 0, 7, p, 0,0,0, 2,2,2, ch);
    EXPECT_EQ(g[ca_cell_index(d,1,1,1)], (uint8_t)VoxMat::Water);   // condensed
}

TEST(Combustion, SteamNextToFireDoesNotCondense) {
    using namespace vox;
    MaterialCaDims d{3, 3};
    std::vector<uint8_t> g((size_t)d.extent*d.extent*d.height_cells, (uint8_t)VoxMat::Air);
    g[ca_cell_index(d,1,1,1)] = (uint8_t)VoxMat::Steam;
    g[ca_cell_index(d,2,1,1)] = (uint8_t)VoxMat::Fire;
    CombustionParams p; p.condense_chance = 1.0f; p.burn_out_chance = 0.0f;
    std::vector<uint32_t> ch;
    combustion_sweep(g, d, 0, 7, p, 0,0,0, 2,2,2, ch);
    EXPECT_EQ(g[ca_cell_index(d,1,1,1)], (uint8_t)VoxMat::Steam);   // still heated -> stays
}

TEST(Combustion, SteamRisesCondensesAndSleeps) {
    using namespace vox;
    MaterialCaDims d{6, 16};
    std::vector<uint8_t> g((size_t)d.extent*d.extent*d.height_cells, (uint8_t)VoxMat::Air);
    for (int iz = 0; iz < d.extent; ++iz)
        for (int ix = 0; ix < d.extent; ++ix)
            for (int iy = 0; iy <= 6; ++iy)
                g[ca_cell_index(d, ix, iy, iz)] = (uint8_t)VoxMat::Water;   // water up to iy=6
    g[ca_cell_index(d, 3, 7, 3)] = (uint8_t)VoxMat::Fire;                   // fire just above the surface
    MaterialCa ca;
    ca.enable_combustion(/*seed*/55, CombustionParams{});                  // default nonzero rates
    ca.wake_box(2, 0, 2, 4, 10, 4);
    std::vector<uint32_t> changed;
    for (int s = 0; s < 2000 && ca.awake(); ++s) { changed.clear(); ca.step(g, d, changed); }
    EXPECT_FALSE(ca.awake());                          // converged & sleeps
    EXPECT_EQ(count_of(g, VoxMat::Fire),  0);           // fire gone
    EXPECT_EQ(count_of(g, VoxMat::Steam), 0);           // all steam condensed
    EXPECT_EQ(count_of(g, VoxMat::Smoke), 0);           // smoke dissipated
}

TEST(Combustion, FireConsumesFuelLeavesAshSmokeDissipatesAndSleeps) {
    using namespace vox;
    MaterialCaDims d{6, 16};
    std::vector<uint8_t> g((size_t)d.extent*d.extent*d.height_cells, (uint8_t)VoxMat::Air);
    int cx = 3, cz = 3;
    for (int iy = 1; iy <= 8; ++iy) g[ca_cell_index(d, cx, iy, cz)] = (uint8_t)VoxMat::Kelp; // fuel column
    g[ca_cell_index(d, cx, 0, cz)] = (uint8_t)VoxMat::Fire;                                  // ignite base
    MaterialCa ca;
    ca.enable_combustion(/*seed*/123, CombustionParams{});   // default rates (all nonzero -> terminates)
    ca.wake_box(cx-1, 0, cz-1, cx+1, 9, cz+1);
    std::vector<uint32_t> changed;
    for (int s = 0; s < 2000 && ca.awake(); ++s) { changed.clear(); ca.step(g, d, changed); }
    EXPECT_FALSE(ca.awake());                       // converged & sleeps
    EXPECT_EQ(count_of(g, VoxMat::Fire),  0);        // fire fully burned out
    EXPECT_EQ(count_of(g, VoxMat::Smoke), 0);        // smoke fully dissipated
    EXPECT_GT(count_of(g, VoxMat::Ash),   0);        // left some ash residue
}

// --- SP3-III Lava micro-tests ---

TEST(Combustion, LavaIgnitesAdjacentFuel) {
    using namespace vox;
    MaterialCaDims d{3, 3};
    std::vector<uint8_t> g((size_t)d.extent*d.extent*d.height_cells, (uint8_t)VoxMat::Air);
    g[ca_cell_index(d,1,1,1)] = (uint8_t)VoxMat::Kelp;   // flammability 0.4
    g[ca_cell_index(d,2,1,1)] = (uint8_t)VoxMat::Lava;
    CombustionParams p; p.ignite_scale = 4.0f;           // 0.4*4 >= 1 -> deterministic ignite
    std::vector<uint32_t> ch;
    combustion_sweep(g, d, 0, 7, p, 0,0,0, 2,2,2, ch);
    EXPECT_EQ(g[ca_cell_index(d,1,1,1)], (uint8_t)VoxMat::Fire);
}

TEST(Combustion, LavaBoilsAdjacentWater) {
    using namespace vox;
    MaterialCaDims d{3, 3};
    std::vector<uint8_t> g((size_t)d.extent*d.extent*d.height_cells, (uint8_t)VoxMat::Air);
    g[ca_cell_index(d,1,1,1)] = (uint8_t)VoxMat::Water;
    g[ca_cell_index(d,2,1,1)] = (uint8_t)VoxMat::Lava;
    CombustionParams p; p.boil_chance = 1.0f; p.cool_chance = 0.0f;  // isolate the water side
    std::vector<uint32_t> ch;
    combustion_sweep(g, d, 0, 7, p, 0,0,0, 2,2,2, ch);
    EXPECT_EQ(g[ca_cell_index(d,1,1,1)], (uint8_t)VoxMat::Steam);
}

TEST(Combustion, LavaCoolsToRockNextToWater) {
    using namespace vox;
    MaterialCaDims d{3, 3};
    std::vector<uint8_t> g((size_t)d.extent*d.extent*d.height_cells, (uint8_t)VoxMat::Air);
    g[ca_cell_index(d,1,1,1)] = (uint8_t)VoxMat::Lava;
    g[ca_cell_index(d,2,1,1)] = (uint8_t)VoxMat::Water;
    CombustionParams p; p.cool_chance = 1.0f; p.boil_chance = 0.0f;  // isolate the lava side
    std::vector<uint32_t> ch;
    combustion_sweep(g, d, 0, 7, p, 0,0,0, 2,2,2, ch);
    EXPECT_EQ(g[ca_cell_index(d,1,1,1)], (uint8_t)VoxMat::Rock);
}

TEST(Combustion, LavaInAirNeverCools) {
    using namespace vox;
    MaterialCaDims d{3, 3};
    std::vector<uint8_t> g((size_t)d.extent*d.extent*d.height_cells, (uint8_t)VoxMat::Air);
    g[ca_cell_index(d,1,1,1)] = (uint8_t)VoxMat::Lava;
    CombustionParams p; p.cool_chance = 1.0f;            // even at certainty, no water -> no cool
    for (uint32_t s = 0; s < 50; ++s) {
        std::vector<uint32_t> ch;
        combustion_sweep(g, d, s, 7, p, 0,0,0, 2,2,2, ch);
    }
    EXPECT_EQ(g[ca_cell_index(d,1,1,1)], (uint8_t)VoxMat::Lava);
}

TEST(Combustion, LavaTouchingWaterKeepsCaAwake) {
    using namespace vox;
    MaterialCaDims d{3, 3};
    std::vector<uint8_t> g((size_t)d.extent*d.extent*d.height_cells, (uint8_t)VoxMat::Air);
    g[ca_cell_index(d,1,1,1)] = (uint8_t)VoxMat::Lava;
    g[ca_cell_index(d,2,1,1)] = (uint8_t)VoxMat::Water;
    MaterialCa ca;
    CombustionParams p; p.cool_chance = 0.0f; p.boil_chance = 0.0f;  // contact persists, nothing changes
    ca.enable_combustion(7, p);
    ca.wake_box(0,0,0, 2,2,2);
    for (int s = 0; s < 12; ++s) { std::vector<uint32_t> ch; ca.step(g, d, ch); }
    EXPECT_TRUE(ca.awake());   // guard kept lava-with-water alive despite no motion/reaction
}

TEST(Combustion, LavaInAirSleeps) {
    using namespace vox;
    MaterialCaDims d{3, 3};
    std::vector<uint8_t> g((size_t)d.extent*d.extent*d.height_cells, (uint8_t)VoxMat::Air);
    g[ca_cell_index(d,1,0,1)] = (uint8_t)VoxMat::Lava;   // resting on the floor, no water near
    MaterialCa ca;
    CombustionParams p; p.cool_chance = 0.0f;
    ca.enable_combustion(7, p);
    ca.wake_box(0,0,0, 2,2,2);
    for (int s = 0; s < 30 && ca.awake(); ++s) { std::vector<uint32_t> ch; ca.step(g, d, ch); }
    EXPECT_FALSE(ca.awake());   // neighbor-aware: no water -> not reactive -> sleeps
    EXPECT_EQ(g[ca_cell_index(d,1,0,1)], (uint8_t)VoxMat::Lava);
}

TEST(Combustion, LavaPourBoilsCondensesAndSleeps) {
    using namespace vox;
    MaterialCaDims d{6, 16};
    std::vector<uint8_t> g((size_t)d.extent*d.extent*d.height_cells, (uint8_t)VoxMat::Air);
    for (int iz = 0; iz < d.extent; ++iz)
        for (int ix = 0; ix < d.extent; ++ix)
            for (int iy = 0; iy <= 8; ++iy)
                g[ca_cell_index(d, ix, iy, iz)] = (uint8_t)VoxMat::Water;   // pool up to iy=8
    for (int iz = 2; iz <= 3; ++iz)                                          // small lava blob at the surface
        for (int ix = 2; ix <= 3; ++ix)
            g[ca_cell_index(d, ix, 9, iz)] = (uint8_t)VoxMat::Lava;
    MaterialCa ca;
    CombustionParams p;
    p.cool_chance = 0.05f;   // slow crusting -> many steps with lava adjacent to water
    p.boil_chance = 0.05f;   // slow boiling
    ca.enable_combustion(/*seed*/55, p);
    ca.wake_box(1, 0, 1, 4, 12, 4);
    int rock0 = count_of(g, VoxMat::Rock);
    for (int s = 0; s < 4000 && ca.awake(); ++s) { std::vector<uint32_t> ch; ca.step(g, d, ch); }
    EXPECT_FALSE(ca.awake());                          // converged & sleeps
    EXPECT_EQ(count_of(g, VoxMat::Steam), 0);          // all steam condensed
    EXPECT_GT(count_of(g, VoxMat::Rock), rock0);       // lava crusted to rock somewhere
    // No stranded lava-water contact at sleep (the guard's guarantee):
    int stranded = 0;
    const int NX[6]={1,-1,0,0,0,0}, NY[6]={0,0,1,-1,0,0}, NZ[6]={0,0,0,0,1,-1};
    for (int z=0; z<d.extent; ++z) for (int y=0; y<d.height_cells; ++y) for (int x=0; x<d.extent; ++x)
        if (g[ca_cell_index(d,x,y,z)] == (uint8_t)VoxMat::Lava)
            for (int k=0;k<6;++k){ int nx=x+NX[k],ny=y+NY[k],nz=z+NZ[k];
                if (nx>=0&&nx<d.extent&&ny>=0&&ny<d.height_cells&&nz>=0&&nz<d.extent
                    && g[ca_cell_index(d,nx,ny,nz)]==(uint8_t)VoxMat::Water) ++stranded; }
    EXPECT_EQ(stranded, 0);
}

// --- SP3-IV equivalence oracle: the live data-driven combustion_sweep must be
// byte-identical to the pre-refactor 8-arm logic. `reference_sweep` below is a
// verbatim copy of that original logic; the test runs both over a seeded mixed
// grid and asserts identical cells AND changed-lists every step. Default rates
// (partial probabilities) exercise the Fire burn-out-fails-then-smoke-fails
// double fall-through that the forced-rate micro-tests cannot reach.
namespace {
inline uint32_t ref_mix32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x;
}
inline float ref_rnd01(int x, int y, int z, uint32_t step, uint32_t seed, uint32_t salt) {
    uint32_t h = ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u) ^ ((uint32_t)z * 83492791u)
               ^ (step * 2654435761u) ^ seed ^ (salt * 2246822519u);
    return (ref_mix32(h) >> 8) * (1.0f / 16777216.0f);
}
inline bool ref_is_fuel(uint8_t m) { return vox::material_props((vox::VoxMat)m).flammability > 0.0f; }
inline bool ref_is_hot(uint8_t m) {
    return m == (uint8_t)vox::VoxMat::Fire || m == (uint8_t)vox::VoxMat::Lava;
}
// Verbatim pre-SP3-IV combustion_sweep (the equivalence oracle).
void reference_sweep(std::vector<uint8_t>& cells, const vox::MaterialCaDims& d,
                     uint32_t step, uint32_t seed, const vox::CombustionParams& p,
                     int x0, int y0, int z0, int x1, int y1, int z1,
                     std::vector<uint32_t>& changed) {
    using namespace vox;
    const std::vector<uint8_t> before = cells;
    auto at = [&](int x, int y, int z) -> uint8_t {
        if (x < 0 || x >= d.extent || y < 0 || y >= d.height_cells || z < 0 || z >= d.extent)
            return (uint8_t)VoxMat::Rock;
        return before[ca_cell_index(d, x, y, z)];
    };
    const int NX[6] = {1,-1,0,0,0,0}, NY[6] = {0,0,1,-1,0,0}, NZ[6] = {0,0,0,0,1,-1};
    for (int z = z0; z <= z1; ++z)
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            uint8_t m = at(x, y, z);
            int idx = ca_cell_index(d, x, y, z);
            bool nbHot = false, nbWater = false, hasAir = false;
            int ax = 0, ay = 0, az = 0;
            for (int k = 0; k < 6; ++k) {
                uint8_t nm = at(x + NX[k], y + NY[k], z + NZ[k]);
                if (ref_is_hot(nm))               nbHot = true;
                if (nm == (uint8_t)VoxMat::Water) nbWater = true;
                if (!hasAir && nm == (uint8_t)VoxMat::Air) { hasAir = true; ax = x+NX[k]; ay = y+NY[k]; az = z+NZ[k]; }
            }
            if (m == (uint8_t)VoxMat::Fire) {
                if (nbWater) { cells[idx] = (uint8_t)VoxMat::Smoke; changed.push_back((uint32_t)idx); continue; }
                if (ref_rnd01(x,y,z,step,seed,2) < p.burn_out_chance) {
                    cells[idx] = (uint8_t)VoxMat::Ash; changed.push_back((uint32_t)idx); continue;
                }
                if (hasAir && ref_rnd01(x,y,z,step,seed,3) < p.smoke_chance) {
                    int aidx = ca_cell_index(d, ax, ay, az);
                    cells[aidx] = (uint8_t)VoxMat::Smoke; changed.push_back((uint32_t)aidx);
                }
                continue;
            }
            if (ref_is_fuel(m) && nbHot) {
                float fl = material_props((VoxMat)m).flammability;
                if (ref_rnd01(x,y,z,step,seed,1) < fl * p.ignite_scale) {
                    cells[idx] = (uint8_t)VoxMat::Fire; changed.push_back((uint32_t)idx);
                }
                continue;
            }
            if (m == (uint8_t)VoxMat::Smoke) {
                if (ref_rnd01(x,y,z,step,seed,4) < p.smoke_dissipate_chance) {
                    cells[idx] = (uint8_t)VoxMat::Air; changed.push_back((uint32_t)idx);
                }
                continue;
            }
            if (m == (uint8_t)VoxMat::Water) {
                if (nbHot && ref_rnd01(x,y,z,step,seed,5) < p.boil_chance) {
                    cells[idx] = (uint8_t)VoxMat::Steam; changed.push_back((uint32_t)idx);
                }
                continue;
            }
            if (m == (uint8_t)VoxMat::Steam) {
                if (!nbHot && ref_rnd01(x,y,z,step,seed,6) < p.condense_chance) {
                    cells[idx] = (uint8_t)VoxMat::Water; changed.push_back((uint32_t)idx);
                }
                continue;
            }
            if (m == (uint8_t)VoxMat::Lava) {
                if (nbWater && ref_rnd01(x,y,z,step,seed,7) < p.cool_chance) {
                    cells[idx] = (uint8_t)VoxMat::Rock; changed.push_back((uint32_t)idx);
                }
                continue;
            }
        }
}
}  // namespace

TEST(Combustion, TableMatchesOriginalArmsOverRandomGrid) {
    using namespace vox;
    MaterialCaDims d{8, 8};
    const size_t n = (size_t)d.extent * d.extent * d.height_cells;
    // Reactive-heavy palette so every arm fires often over the run.
    const VoxMat palette[] = {
        VoxMat::Air, VoxMat::Water, VoxMat::Fire, VoxMat::Smoke, VoxMat::Steam,
        VoxMat::Lava, VoxMat::Kelp, VoxMat::Boat, VoxMat::Rock, VoxMat::Sand,
    };
    const int kPal = (int)(sizeof(palette) / sizeof(palette[0]));
    // Deterministic LCG fill (no Date/rand — reproducible).
    std::vector<uint8_t> g(n);
    uint32_t lcg = 0x1234567u;
    for (size_t i = 0; i < n; ++i) {
        lcg = lcg * 1664525u + 1013904223u;
        g[i] = (uint8_t)palette[(lcg >> 16) % kPal];
    }
    std::vector<uint8_t> a = g, b = g;     // oracle copy / live copy
    const uint32_t seed = 9001;
    CombustionParams p;                    // default rates: all partial -> fall-through exercised
    for (uint32_t step = 0; step < 64; ++step) {
        std::vector<uint32_t> ca, cb;
        reference_sweep   (a, d, step, seed, p, 0,0,0, d.extent-1, d.height_cells-1, d.extent-1, ca);
        combustion_sweep  (b, d, step, seed, p, 0,0,0, d.extent-1, d.height_cells-1, d.extent-1, cb);
        ASSERT_EQ(a, b)   << "cells diverged at step " << step;
        ASSERT_EQ(ca, cb) << "changed-list diverged at step " << step;
    }
}

// --- Heat diffusion (thermal_sweep) ------------------------------------------
TEST(ThermalSweep, HeatSourceWarmsNeighbourAndConductivityOrders) {
    MaterialCaDims d{5,5};
    auto idx = [&](int x,int y,int z){ return ca_cell_index(d,x,y,z); };
    ThermalParams tp;
    std::vector<uint32_t> changed;
    auto run = [&](VoxMat medium, int steps){
        std::vector<uint8_t> cells((size_t)5*5*5, (uint8_t)medium);
        std::vector<uint8_t> temp(cells.size(), kAmbientTemp);
        cells[idx(2,2,2)] = (uint8_t)VoxMat::Lava;   // emit_temp=255 heat source
        temp [idx(2,2,2)] = 255;
        for (int s=0;s<steps;++s){ changed.clear();
            thermal_sweep(cells,temp,d,tp,kAmbientTemp, 0,0,0,4,4,4, changed); }
        return temp[idx(2,3,2)];
    };
    uint8_t rock  = run(VoxMat::Rock,  6);   // conductivity 0.50
    uint8_t water = run(VoxMat::Water, 6);   // conductivity 0.30
    EXPECT_GT(rock,  kAmbientTemp);
    EXPECT_GT(water, kAmbientTemp);
    EXPECT_GT(rock,  water);
}

TEST(ThermalSweep, RelaxesTowardAmbientWithoutSource) {
    MaterialCaDims d{5,5};
    auto idx = [&](int x,int y,int z){ return ca_cell_index(d,x,y,z); };
    std::vector<uint8_t> cells((size_t)5*5*5, (uint8_t)VoxMat::Rock);
    std::vector<uint8_t> temp(cells.size(), kAmbientTemp);
    temp[idx(2,2,2)] = 200;
    ThermalParams tp; std::vector<uint32_t> changed;
    for (int s=0;s<200;++s){ changed.clear();
        thermal_sweep(cells,temp,d,tp,kAmbientTemp, 0,0,0,4,4,4, changed); }
    for (uint8_t t : temp) EXPECT_EQ(t, kAmbientTemp);
}

// --- Thermal-threshold transitions -------------------------------------------
TEST(ThermalRules, WaterBoilsAndFlammableIgnitesAboveThreshold) {
    MaterialCaDims d{3,3};
    auto idx=[&](int x,int y,int z){ return ca_cell_index(d,x,y,z); };
    std::vector<uint8_t> cells((size_t)3*3*3,(uint8_t)VoxMat::Air);
    std::vector<uint8_t> temp(cells.size(), kAmbientTemp);
    cells[idx(1,1,1)] = (uint8_t)VoxMat::Water; temp[idx(1,1,1)] = 200;  // hot water
    cells[idx(0,1,1)] = (uint8_t)VoxMat::Boat;  temp[idx(0,1,1)] = 200;  // hot fuel
    ThermalParams tp; std::vector<uint32_t> changed;
    thermal_sweep(cells,temp,d,tp,kAmbientTemp, 0,0,0,2,2,2, changed);
    EXPECT_EQ(cells[idx(1,1,1)], (uint8_t)VoxMat::Steam);   // boiled
    EXPECT_EQ(cells[idx(0,1,1)], (uint8_t)VoxMat::Fire);    // ignited
    EXPECT_FALSE(changed.empty());
}

TEST(ThermalRules, SteamCondensesBelowThreshold) {
    MaterialCaDims d{3,3};
    auto idx=[&](int x,int y,int z){ return ca_cell_index(d,x,y,z); };
    std::vector<uint8_t> cells((size_t)3*3*3,(uint8_t)VoxMat::Air);
    std::vector<uint8_t> temp(cells.size(), kAmbientTemp);   // ambient 25 < condense 80
    cells[idx(1,1,1)] = (uint8_t)VoxMat::Steam;
    ThermalParams tp; std::vector<uint32_t> changed;
    thermal_sweep(cells,temp,d,tp,kAmbientTemp, 0,0,0,2,2,2, changed);
    EXPECT_EQ(cells[idx(1,1,1)], (uint8_t)VoxMat::Water);    // condensed
}
