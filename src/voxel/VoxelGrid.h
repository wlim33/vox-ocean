#pragma once
#include <cassert>
namespace vox {
// extent = columns per axis; preconditions: all fields > 0 (Config layer clamps upstream).
struct VoxelGridParams {
    int   extent;
    float voxel_size_m;
    float height_step_m;
    float base_depth_m;
};
// CPU mirror of shaders/voxelize.metal — keep the math in lockstep.
class VoxelGrid {
public:
    explicit VoxelGrid(VoxelGridParams p) : p_(p) {
        assert(p_.extent > 0);
        assert(p_.voxel_size_m > 0.0f);
        assert(p_.height_step_m > 0.0f);
    }
    int   columns() const { return p_.extent * p_.extent; }
    float patch_size_m() const { return p_.extent * p_.voxel_size_m; }
    float column_center_x(int ix) const;
    float column_center_z(int iz) const { return column_center_x(iz); }
    float quantize_height(float h) const;
    const VoxelGridParams& params() const { return p_; }
private:
    VoxelGridParams p_;
};
}
