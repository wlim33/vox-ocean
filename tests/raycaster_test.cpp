#include "voxel/Raycaster.h"
#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

// extent 4, 4 cells tall, 1m voxels, base 2m down: AABB x,z in [-2,2], y in [-2,2].
static vox::VoxelWorld world() { return vox::VoxelWorld({4, 4, 1.0f, 1.0f, 2.0f}); }
static std::vector<uint8_t> empty_grid(const vox::VoxelWorld& w) {
    return std::vector<uint8_t>((size_t)w.cells(), (uint8_t)vox::VoxMat::Air);
}

// Camera 50m straight above origin, looking down; up vector along -Z.
static glm::mat4 topdown_inv_vp(glm::vec3 eye) {
    glm::mat4 view = glm::lookAt(eye, glm::vec3(0, 0, 0), glm::vec3(0, 0, -1));
    glm::mat4 proj = glm::perspective(glm::radians(55.0f), 1.0f, 0.5f, 5000.0f);
    return glm::inverse(proj * view);
}

TEST(Raycaster, CenterPixelHitsCellUnderCrosshair) {
    auto w = world(); auto g = empty_grid(w);
    for (int ix = 0; ix < 4; ++ix)
        for (int iz = 0; iz < 4; ++iz)
            g[w.cell_index(ix, 0, iz)] = (uint8_t)vox::VoxMat::Rock;  // floor layer
    glm::vec3 eye{0, 50, 0};
    auto h = vox::pick(100, 100, 50, 50, topdown_inv_vp(eye), eye, w, g.data(), 256);
    ASSERT_TRUE(h.has_value());
    EXPECT_EQ(h->material, (uint8_t)vox::VoxMat::Rock);
    EXPECT_EQ(h->iy, 0);                       // hit the floor, not the air above it
    EXPECT_EQ(h->face_axis, 1);                // entered through a y face
    EXPECT_EQ(h->linear_idx, (uint32_t)w.cell_index(h->ix, h->iy, h->iz));
}

TEST(Raycaster, RayMissingGridReturnsNullopt) {
    auto w = world(); auto g = empty_grid(w);  // all air
    glm::vec3 eye{0, 50, 0};
    auto h = vox::pick(100, 100, 50, 50, topdown_inv_vp(eye), eye, w, g.data(), 256);
    EXPECT_FALSE(h.has_value());
}

TEST(Raycaster, ZeroViewportReturnsNullopt) {
    auto w = world(); auto g = empty_grid(w);
    auto h = vox::pick(0, 0, 0, 0, glm::mat4(1.0f), {0, 0, 0}, w, g.data(), 256);
    EXPECT_FALSE(h.has_value());
}

TEST(Raycaster, ScreenYFlipMapsTopToNegativeZ) {
    // Pins the NDC y-flip: top-of-screen must map toward the camera's up vector (-Z).
    auto w = world(); auto g = empty_grid(w);
    for (int ix = 0; ix < 4; ++ix)
        for (int iz = 0; iz < 4; ++iz)
            g[w.cell_index(ix, 0, iz)] = (uint8_t)vox::VoxMat::Rock;
    glm::vec3 eye{0, 50, 0};
    auto inv = topdown_inv_vp(eye);
    auto top = vox::pick(100, 100, 50, 48, inv, eye, w, g.data(), 256);  // py small = top
    auto bot = vox::pick(100, 100, 50, 52, inv, eye, w, g.data(), 256);  // py large = bottom
    ASSERT_TRUE(top.has_value()); ASSERT_TRUE(bot.has_value());
    EXPECT_LT(top->iz, bot->iz);              // top of screen -> -Z -> smaller iz
}
