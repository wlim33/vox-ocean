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
