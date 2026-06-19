#include <metal_stdlib>
#include "shader_types.h"
#include "voxel_grid.h"
using namespace metal;

// One thread per column: the water surface is flat (mean sea level y=0) plus the
// interactive ripple layer. Mirror of src/voxel/VoxelWorld quantization.
kernel void world_fill(
    constant WorldFillUniforms& U               [[buffer(0)]],
    texture2d<float, access::write> surface      [[texture(0)]],
    texture2d<float, access::read>  ripple       [[texture(1)]],
    uint2 gid [[thread_position_in_grid]])
{
    if ((int)gid.x >= U.grid_extent || (int)gid.y >= U.grid_extent) return;
    VoxelGridDesc g = {U.grid_extent, U.height_cells, U.voxel_size_m, U.height_step_m, U.base_depth_m};
    float ripple_h = ripple.read(gid).x;
    float top = vg_quantize_height(g, ripple_h);    // about mean sea level y=0
    // Foam baseline 1.0 (no wave folding); ripple impacts lower it.
    surface.write(float4(top, 1.0 - U.ripple_foam * abs(ripple_h), 0.0, 0.0), gid);
}
