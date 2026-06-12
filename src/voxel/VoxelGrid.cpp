#include "voxel/VoxelGrid.h"
#include <algorithm>
#include <cmath>
namespace vox {
float VoxelGrid::column_center_x(int ix) const {
    return (ix + 0.5f) * p_.voxel_size_m - 0.5f * patch_size_m();
}
float VoxelGrid::quantize_height(float h) const {
    float q = std::floor(h / p_.height_step_m) * p_.height_step_m;
    return std::max(q, -p_.base_depth_m + p_.height_step_m);
}
}
