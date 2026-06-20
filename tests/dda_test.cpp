#include "voxel/Dda.h"
#include <gtest/gtest.h>
#include <vector>

// extent 4, 4 cells tall, 1m voxels, 1m steps, base 2m down:
// AABB x,z in [-2,2], y in [-2,2].
static vox::VoxelWorld world() { return vox::VoxelWorld({4, 4, 1.0f, 1.0f, 2.0f}); }

static std::vector<uint8_t> empty_grid(const vox::VoxelWorld& w) {
    return std::vector<uint8_t>((size_t)w.cells(), (uint8_t)vox::VoxMat::Air);
}

TEST(Dda, MissesEmptyGrid) {
    auto w = world(); auto g = empty_grid(w);
    auto r = vox::dda_march({-5.0f, -0.5f, -0.5f}, {1, 0, 0}, w, g.data(), 64);
    EXPECT_FALSE(r.hit);
    EXPECT_LE(r.steps, 5);              // exits after at most extent+1 cells
}

TEST(Dda, MissesEntirely) {
    auto w = world(); auto g = empty_grid(w);
    auto r = vox::dda_march({-5.0f, 10.0f, 0.0f}, {1, 0, 0}, w, g.data(), 64);
    EXPECT_FALSE(r.hit);
    EXPECT_EQ(r.steps, 0);              // never enters the AABB
}

TEST(Dda, HitsFirstSolidAlongX) {
    auto w = world(); auto g = empty_grid(w);
    g[w.cell_index(2, 1, 1)] = (uint8_t)vox::VoxMat::Rock;
    // y=-0.5 -> iy = 1, z=-0.5 -> iz = 1
    auto r = vox::dda_march({-5.0f, -0.5f, -0.5f}, {1, 0, 0}, w, g.data(), 64);
    ASSERT_TRUE(r.hit);
    EXPECT_EQ(r.ix, 2); EXPECT_EQ(r.iy, 1); EXPECT_EQ(r.iz, 1);
    EXPECT_EQ(r.face_axis, 0);          // entered the cell through an x face
    EXPECT_NEAR(r.t, 5.0f, 0.01f);  // origin -5 -> cell boundary at x=0
}

TEST(Dda, HitsTopSurfaceFromAbove) {
    auto w = world(); auto g = empty_grid(w);
    for (int ix = 0; ix < 4; ++ix)
        for (int iz = 0; iz < 4; ++iz)
            for (int iy = 0; iy < 2; ++iy)   // water filled to y = 0
                g[w.cell_index(ix, iy, iz)] = (uint8_t)vox::VoxMat::Water;
    auto r = vox::dda_march({0.5f, 10.0f, 0.5f}, {0, -1, 0}, w, g.data(), 64);
    ASSERT_TRUE(r.hit);
    EXPECT_EQ(r.iy, 1);                 // topmost filled cell
    EXPECT_EQ(r.face_axis, 1);
}

TEST(Dda, RespectsMaxSteps) {
    auto w = world(); auto g = empty_grid(w);
    g[w.cell_index(3, 1, 1)] = (uint8_t)vox::VoxMat::Rock;
    auto r = vox::dda_march({-5.0f, -0.5f, -0.5f}, {1, 0, 0}, w, g.data(), 2);
    EXPECT_FALSE(r.hit);                // budget exhausted before reaching ix=3
    EXPECT_EQ(r.steps, 2);
}

TEST(Dda, DiagonalTraversal) {
    auto w = world(); auto g = empty_grid(w);
    g[w.cell_index(3, 3, 3)] = (uint8_t)vox::VoxMat::Rock;
    auto r = vox::dda_march({-3.0f, -3.0f, -3.0f},
                            glm::normalize(glm::vec3(1, 1, 1)), w, g.data(), 64);
    ASSERT_TRUE(r.hit);
    EXPECT_EQ(r.ix, 3); EXPECT_EQ(r.iy, 3); EXPECT_EQ(r.iz, 3);
}

TEST(Dda, HitsFirstSolidAlongNegX) {
    // Pins the negative-direction stepping branch (step = -1 boundary pick).
    auto w = world(); auto g = empty_grid(w);
    g[w.cell_index(1, 1, 1)] = (uint8_t)vox::VoxMat::Rock;
    auto r = vox::dda_march({5.0f, -0.5f, -0.5f}, {-1, 0, 0}, w, g.data(), 64);
    ASSERT_TRUE(r.hit);
    EXPECT_EQ(r.ix, 1); EXPECT_EQ(r.iy, 1); EXPECT_EQ(r.iz, 1);
    EXPECT_EQ(r.face_axis, 0);
    EXPECT_NEAR(r.t, 5.0f, 0.01f);   // cell ix=1 entered at x = 0 from above
    EXPECT_EQ(r.steps, 3);
}

TEST(Dda, DiagonalTieBreakOrder) {
    // Pins the simultaneous-boundary advance order (x-before-y-before-z via
    // strict < tie-breaks): cells visited on a perfect diagonal is exactly 10.
    auto w = world(); auto g = empty_grid(w);
    g[w.cell_index(3, 3, 3)] = (uint8_t)vox::VoxMat::Rock;
    auto r = vox::dda_march({-3.0f, -3.0f, -3.0f},
                            glm::normalize(glm::vec3(1, 1, 1)), w, g.data(), 64);
    ASSERT_TRUE(r.hit);
    EXPECT_EQ(r.steps, 10);
}

// ---- dda_march_transmit: the M2 see-through-water walk ----------------
// CPU mirror of voxel_march.metal's three-phase march (to-first-hit, bend
// once, transmit). Hand-derived expectations; tolerances absorb the 1e-4
// entry nudges.

TEST(DdaTransmit, AllAirBehavesLikeMiss) {
    auto w = world(); auto g = empty_grid(w);
    auto r = vox::dda_march_transmit({-5.0f, -0.5f, -0.5f}, {1, 0, 0}, w, g.data(), 64, 1.33f);
    EXPECT_FALSE(r.hit);
    EXPECT_EQ(r.entry_axis, -1);          // never touched water
    EXPECT_FLOAT_EQ(r.water_dist, 0.0f);
}

TEST(DdaTransmit, StraightDownThroughWaterOntoSand) {
    // Sand at iy=0, water iy=1..2 (surface y = 1), air iy=3. A vertical ray
    // refracts straight-on (no bend), crosses exactly 2m of water.
    auto w = world(); auto g = empty_grid(w);
    for (int ix = 0; ix < 4; ++ix)
        for (int iz = 0; iz < 4; ++iz) {
            g[w.cell_index(ix, 0, iz)] = (uint8_t)vox::VoxMat::Sand;
            g[w.cell_index(ix, 1, iz)] = (uint8_t)vox::VoxMat::Water;
            g[w.cell_index(ix, 2, iz)] = (uint8_t)vox::VoxMat::Water;
        }
    auto r = vox::dda_march_transmit({0.5f, 10.0f, 0.5f}, {0, -1, 0}, w, g.data(), 64, 1.33f);
    ASSERT_TRUE(r.hit);
    EXPECT_EQ(r.ix, 2); EXPECT_EQ(r.iy, 0); EXPECT_EQ(r.iz, 2);   // sand under (0.5, ., 0.5)
    EXPECT_EQ(r.entry_axis, 1);
    EXPECT_EQ(r.opaque_axis, 1);          // sand entered from above
    EXPECT_NEAR(r.entry_t, 9.0f, 0.01f);   // y=10 down to surface y=1
    EXPECT_NEAR(r.water_dist, 2.0f, 0.01f);
}

TEST(DdaTransmit, SideEntryStraightOnAccumulatesFullWidth) {
    // Water everywhere iy=0..2; a horizontal ray entering the side wall
    // straight-on doesn't bend, crosses the full 4m patch, exits sideways.
    auto w = world(); auto g = empty_grid(w);
    for (int ix = 0; ix < 4; ++ix)
        for (int iz = 0; iz < 4; ++iz)
            for (int iy = 0; iy < 3; ++iy)
                g[w.cell_index(ix, iy, iz)] = (uint8_t)vox::VoxMat::Water;
    auto r = vox::dda_march_transmit({-5.0f, 0.5f, -0.5f}, {1, 0, 0}, w, g.data(), 64, 1.33f);
    EXPECT_FALSE(r.hit);                   // no opaque cell on the path
    EXPECT_FALSE(r.exited_up);             // left through the far side wall
    EXPECT_EQ(r.entry_axis, 0);
    EXPECT_NEAR(r.water_dist, 4.0f, 0.01f);
}

TEST(DdaTransmit, ObliqueEntryBendsTowardNormal) {
    // Sand iy=0, water iy=1..2 (surface y = 1). Ray dir (1,-2,0)/sqrt(5) hits
    // the surface at (0.5, 1, -0.5). Snell with IOR 1.33: refracted dir
    // (0.3363, -0.9418, 0); water depth 2m -> path 2/0.9418 = 2.124m; lands
    // on sand at x = 0.5 + 0.3363*2.124 = 1.214 -> ix 3 (cell [1,2)).
    auto w = world(); auto g = empty_grid(w);
    for (int ix = 0; ix < 4; ++ix)
        for (int iz = 0; iz < 4; ++iz) {
            g[w.cell_index(ix, 0, iz)] = (uint8_t)vox::VoxMat::Sand;
            g[w.cell_index(ix, 1, iz)] = (uint8_t)vox::VoxMat::Water;
            g[w.cell_index(ix, 2, iz)] = (uint8_t)vox::VoxMat::Water;
        }
    auto r = vox::dda_march_transmit({-1.5f, 5.0f, -0.5f},
                                     glm::normalize(glm::vec3(1, -2, 0)),
                                     w, g.data(), 64, 1.33f);
    ASSERT_TRUE(r.hit);
    EXPECT_EQ(r.ix, 3); EXPECT_EQ(r.iy, 0); EXPECT_EQ(r.iz, 1);
    EXPECT_EQ(r.entry_axis, 1);
    EXPECT_EQ(r.opaque_axis, 1);
    EXPECT_NEAR(r.entry_t, 4.472f, 0.02f);   // |(0.5,1)-( -1.5,5)| = sqrt(20)
    EXPECT_NEAR(r.water_dist, 2.124f, 0.02f);
    // IOR 1: same geometry, no bend — shallower underwater angle, so at
    // least as far in x and a longer in-water path.
    auto s = vox::dda_march_transmit({-1.5f, 5.0f, -0.5f},
                                     glm::normalize(glm::vec3(1, -2.1f, 0)),
                                     w, g.data(), 64, 1.0f);
    ASSERT_TRUE(s.hit);
    // No bend: unit dir (0.4300,-0.9029,0); 2m water depth -> path 2.215m,
    // landing x = 0.405 + 0.4300*2.215 = 1.357 -> ix 3.
    EXPECT_EQ(s.ix, 3);
    EXPECT_NEAR(s.water_dist, 2.215f, 0.02f);
}

TEST(DdaTransmit, UpwardExitSamplesSky) {
    // Water slab iy=2 only (y in [0,1]); ray enters its side wall tilted up,
    // refraction flattens the tilt (sin 0.1961 -> 0.1475), it leaves the slab
    // top after 0.678m of water and eventually exits the grid still rising.
    auto w = world(); auto g = empty_grid(w);
    for (int ix = 0; ix < 4; ++ix)
        for (int iz = 0; iz < 4; ++iz)
            g[w.cell_index(ix, 2, iz)] = (uint8_t)vox::VoxMat::Water;
    auto r = vox::dda_march_transmit({-5.0f, 0.3f, -0.5f},
                                     glm::normalize(glm::vec3(1, 0.2f, 0)),
                                     w, g.data(), 64, 1.33f);
    EXPECT_FALSE(r.hit);
    EXPECT_EQ(r.entry_axis, 0);
    EXPECT_TRUE(r.exited_up);
    EXPECT_NEAR(r.water_dist, 0.678f, 0.03f);
}

TEST(DdaTransmit, BudgetExhaustionMidWaterIsPartial) {
    // Water everywhere iy=0..2; straight-on side entry, budget of 4 steps:
    // 1 step finds water, 3 transmit steps cross 3m before the budget ends.
    auto w = world(); auto g = empty_grid(w);
    for (int ix = 0; ix < 4; ++ix)
        for (int iz = 0; iz < 4; ++iz)
            for (int iy = 0; iy < 3; ++iy)
                g[w.cell_index(ix, iy, iz)] = (uint8_t)vox::VoxMat::Water;
    auto r = vox::dda_march_transmit({-5.0f, 0.5f, -0.5f}, {1, 0, 0}, w, g.data(), 4, 1.33f);
    EXPECT_FALSE(r.hit);                  // degraded path: budget, not geometry
    EXPECT_EQ(r.entry_axis, 0);
    EXPECT_FALSE(r.exited_up);
    EXPECT_EQ(r.steps, 4);
    EXPECT_NEAR(r.water_dist, 3.0f, 0.05f);
}

TEST(DdaTransmit, BubbleIsAClearGapInWater) {
    // Compare two identical vertical water columns; one has a Bubble in the middle.
    // The bubble must be passed through (ray still reaches the sand floor) and must
    // NOT absorb, so its water_dist is strictly less than the all-water column.
    vox::VoxelWorld w(vox::VoxelWorldParams{4, 8, 0.5f, 0.25f, 2.0f});
    auto make = [&](bool bubble) {
        std::vector<uint8_t> g((size_t)4 * 4 * 8, (uint8_t)vox::VoxMat::Air);
        for (int iz = 0; iz < 4; ++iz)
            for (int ix = 0; ix < 4; ++ix) {
                g[w.cell_index(ix, 0, iz)] = (uint8_t)vox::VoxMat::Sand;   // floor
                g[w.cell_index(ix, 1, iz)] = (uint8_t)vox::VoxMat::Water;
                g[w.cell_index(ix, 2, iz)] = (uint8_t)(bubble ? vox::VoxMat::Bubble : vox::VoxMat::Water);
                g[w.cell_index(ix, 3, iz)] = (uint8_t)vox::VoxMat::Water;
            }
        return g;
    };
    auto all   = make(false);
    auto withb = make(true);
    auto r_all = vox::dda_march_transmit({0.5f, 10.0f, 0.5f}, {0, -1, 0}, w, all.data(),   64, 1.33f);
    auto r_bub = vox::dda_march_transmit({0.5f, 10.0f, 0.5f}, {0, -1, 0}, w, withb.data(), 64, 1.33f);
    EXPECT_TRUE(r_all.hit);
    EXPECT_TRUE(r_bub.hit);                                  // ray passes through the bubble to the sand
    EXPECT_LT(r_bub.water_dist, r_all.water_dist);          // bubble segment is not absorbed
    EXPECT_NEAR(r_bub.water_dist, r_all.water_dist * 2.0f / 3.0f, 0.05f); // 2 of 3 cells absorb
}
