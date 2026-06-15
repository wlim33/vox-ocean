#include "core/AxisCamera.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/geometric.hpp>
#include <cassert>

namespace vox {

CameraView axis_ortho_view(AxisShot shot, VoxelGridDesc grid, float pad, float cell_aspect) {
    assert(cell_aspect > 0.0f && "cell_aspect must be > 0");
    assert(pad >= 0.0f && "pad must be >= 0");
    const float H     = vg_half_patch(grid);
    const float topY  = vg_world_top_y(grid);
    const float baseY = -grid.base_depth_m;
    const glm::vec3 bmin{-H, baseY, -H};
    const glm::vec3 bmax{ H, topY,  H};
    const glm::vec3 center = 0.5f * (bmin + bmax);
    const float eyH = 0.5f * (topY - baseY);   // half-extent in Y

    glm::vec3 axisN{0.0f}, up{0.0f};
    float uHalf = 0, vHalf = 0, dHalf = 0;     // image-horiz / image-vert / depth half-extents
    switch (shot.axis) {
        case ViewAxis::Y: axisN = {0, 1, 0}; up = {0, 0, -1}; uHalf = H; vHalf = H;   dHalf = eyH; break;
        case ViewAxis::Z: axisN = {0, 0, 1}; up = {0, 1, 0};  uHalf = H; vHalf = eyH; dHalf = H;   break;
        case ViewAxis::X: axisN = {1, 0, 0}; up = {0, 1, 0};  uHalf = H; vHalf = eyH; dHalf = H;   break;
    }
    // from_positive=false flips handedness; for the Y (top) axis this yields a
    // horizontal mirror of the top-down view (a bottom-up look).
    const float sign = shot.from_positive ? 1.0f : -1.0f;
    const float dist = 2.0f * (dHalf + pad);
    const glm::vec3 eye = center + axisN * (sign * dist);

    // Pad, then letterbox to the output cell so content keeps proportions.
    float uH = uHalf + pad, vH = vHalf + pad;
    const float contentA = uH / vH;
    if (cell_aspect >= contentA) uH = vH * cell_aspect;
    else                         vH = uH / cell_aspect;

    const float zn = dist - (dHalf + pad);
    const float zf = dist + (dHalf + pad);

    glm::mat4 proj = glm::ortho(-uH, uH, -vH, vH, zn, zf);
    glm::mat4 view = glm::lookAt(eye, center, up);

    CameraView cv;
    cv.view_proj    = proj * view;
    cv.position     = eye;
    cv.ortho_backup = 2.0f * glm::length(bmax - bmin);
    return cv;
}

}
