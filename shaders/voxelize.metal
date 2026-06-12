#include <metal_stdlib>
#include "shader_types.h"
using namespace metal;

// One thread per column: sample cascade displacement, quantize to voxel steps.
// Mirror of src/voxel/VoxelGrid — keep the math in lockstep.
kernel void voxelize(
    constant VoxelizeUniforms& U                       [[buffer(0)]],
    device VoxelInstance* instances                    [[buffer(1)]],
    array<texture2d<float>, MAX_CASCADES> disp_tex     [[texture(0)]],
    array<texture2d<float>, MAX_CASCADES> normal_tex   [[texture(MAX_CASCADES)]],
    uint2 gid [[thread_position_in_grid]])
{
    if ((int)gid.x >= U.grid_extent || (int)gid.y >= U.grid_extent) return;
    constexpr sampler smp(filter::linear, address::repeat);

    float half_patch = 0.5 * U.grid_extent * U.voxel_size_m;
    float2 xz = (float2(gid) + 0.5) * U.voxel_size_m - half_patch;

    float h = 0.0;
    float fold_min = 1.0;
    for (int i = 0; i < U.cascade_count; ++i) {
        float2 uv = xz / U.cascade_size[i];
        h += disp_tex[i].sample(smp, uv, level(0)).y;   // vertical displacement only
        fold_min = min(fold_min, normal_tex[i].sample(smp, uv, level(0)).w);
    }
    // Mirror of VoxelGrid::quantize_height (floor policy)
    float top = floor(h / U.height_step_m) * U.height_step_m;
    top = max(top, -U.base_depth_m + U.height_step_m);

    uint idx = gid.y * (uint)U.grid_extent + gid.x;
    instances[idx].top_y    = top;
    instances[idx].fold_min = fold_min;
}
