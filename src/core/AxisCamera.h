#pragma once
#include "core/CameraView.h"
#include "core/ViewAxis.h"
#include "voxel_grid.h"   // VoxelGridDesc, vg_half_patch, vg_world_top_y
namespace vox {

struct AxisShot {
    ViewAxis axis;        // the axis the camera looks along
    bool from_positive;   // camera on the + side of that axis, looking toward -
};

// Orthographic camera framing the whole diorama AABB along `shot`. `pad` (>= 0)
// adds world-unit margin; `cell_aspect` (out_w / out_h, must be > 0) widens the
// ortho box so content keeps its proportions inside the output cell (no stretching).
CameraView axis_ortho_view(AxisShot shot, VoxelGridDesc grid, float pad, float cell_aspect);

}
