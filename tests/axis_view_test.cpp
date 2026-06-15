#include <gtest/gtest.h>
#include "core/AxisCamera.h"
#include <glm/glm.hpp>
#include <cmath>

using namespace vox;

// extent=64, voxel=0.5 -> H=16; height_cells=32 * 0.25 = 8; base_depth=4 (16 steps).
// world y in [-4, 4], x,z in [-16, 16]. base_depth must be a multiple of height_step.
static const VoxelGridDesc kGrid{64, 32, 0.5f, 0.25f, 4.0f};

static glm::vec3 to_ndc(const CameraView& cv, glm::vec3 world) {
    glm::vec4 c = cv.view_proj * glm::vec4(world, 1.0f);
    return glm::vec3(c) / c.w;
}

TEST(AxisOrthoView, CenterMapsToNdcOrigin) {
    float cy = 0.5f * (vg_world_top_y(kGrid) + (-kGrid.base_depth_m));
    glm::vec3 center{0.0f, cy, 0.0f};
    for (ViewAxis a : {ViewAxis::X, ViewAxis::Y, ViewAxis::Z}) {
        CameraView cv = axis_ortho_view({a, true}, kGrid, 1.0f, 1.0f);
        glm::vec3 n = to_ndc(cv, center);
        EXPECT_NEAR(n.x, 0.0f, 1e-4f);
        EXPECT_NEAR(n.y, 0.0f, 1e-4f);
    }
}

TEST(AxisOrthoView, EnclosesWholeAabb) {
    float H = vg_half_patch(kGrid), topY = vg_world_top_y(kGrid), baseY = -kGrid.base_depth_m;
    glm::vec3 corners[8] = {
        {-H, baseY, -H}, {H, baseY, -H}, {-H, topY, -H}, {H, topY, -H},
        {-H, baseY,  H}, {H, baseY,  H}, {-H, topY,  H}, {H, topY,  H}};
    for (ViewAxis a : {ViewAxis::X, ViewAxis::Y, ViewAxis::Z}) {
        CameraView cv = axis_ortho_view({a, true}, kGrid, 1.0f, 1.0f);
        for (auto& c : corners) {
            glm::vec3 n = to_ndc(cv, c);
            EXPECT_LE(std::abs(n.x), 1.0f + 1e-3f);
            EXPECT_LE(std::abs(n.y), 1.0f + 1e-3f);
            EXPECT_GE(n.z, -1.0f - 1e-3f);
            EXPECT_LE(n.z,  1.0f + 1e-3f);
        }
    }
}

TEST(AxisOrthoView, IsParallelProjection) {
    CameraView cv = axis_ortho_view({ViewAxis::Z, true}, kGrid, 1.0f, 1.0f);
    glm::vec3 p0{1.0f, 0.5f, -2.0f}, p1{1.0f, 0.5f, 3.0f};  // differ only in Z (view axis)
    glm::vec3 n0 = to_ndc(cv, p0), n1 = to_ndc(cv, p1);
    EXPECT_NEAR(n0.x, n1.x, 1e-4f);
    EXPECT_NEAR(n0.y, n1.y, 1e-4f);
}

TEST(AxisOrthoView, PlusYIsUpInFrontAndSideViews) {
    glm::vec3 lo{0, -kGrid.base_depth_m, 0}, hi{0, vg_world_top_y(kGrid), 0};
    for (ViewAxis a : {ViewAxis::Z, ViewAxis::X}) {
        CameraView cv = axis_ortho_view({a, true}, kGrid, 1.0f, 1.0f);
        EXPECT_GT(to_ndc(cv, hi).y, to_ndc(cv, lo).y);
    }
}

TEST(AxisOrthoView, OrthoBackupIsPositive) {
    CameraView cv = axis_ortho_view({ViewAxis::Y, true}, kGrid, 1.0f, 1.0f);
    EXPECT_GT(cv.ortho_backup, 0.0f);
}

TEST(AxisOrthoView, LetterboxAndBackViewsStillEnclose) {
    float H = vg_half_patch(kGrid), topY = vg_world_top_y(kGrid), baseY = -kGrid.base_depth_m;
    glm::vec3 corners[8] = {
        {-H, baseY, -H}, {H, baseY, -H}, {-H, topY, -H}, {H, topY, -H},
        {-H, baseY,  H}, {H, baseY,  H}, {-H, topY,  H}, {H, topY,  H}};
    // (a) non-square cell_aspect exercises the letterbox branch;
    // (b) from_positive=false exercises the flipped-eye path.
    AxisShot shots[2] = { {ViewAxis::Z, true}, {ViewAxis::Y, false} };
    float aspects[2] = { 2.0f, 1.0f };
    for (int k = 0; k < 2; ++k) {
        CameraView cv = axis_ortho_view(shots[k], kGrid, 1.0f, aspects[k]);
        for (auto& c : corners) {
            glm::vec3 n = to_ndc(cv, c);
            EXPECT_LE(std::abs(n.x), 1.0f + 1e-3f);
            EXPECT_LE(std::abs(n.y), 1.0f + 1e-3f);
        }
    }
}
