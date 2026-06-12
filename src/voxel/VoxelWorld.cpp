#include "voxel/VoxelWorld.h"
#include <algorithm>
#include <cmath>
namespace vox {
float VoxelWorld::column_center_x(int ix) const {
    return (ix + 0.5f) * p_.voxel_size_m - 0.5f * patch_size_m();
}
float VoxelWorld::quantize_height(float h) const {
    float q = std::floor(h / p_.height_step_m) * p_.height_step_m;
    return std::max(q, -p_.base_depth_m + p_.height_step_m);
}
int VoxelWorld::water_top_cell(float h) const {
    float top = quantize_height(h);
    int n = (int)std::floor((top + p_.base_depth_m) / p_.height_step_m + 0.5f);
    return std::clamp(n, 1, p_.height_cells);
}
}
