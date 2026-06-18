#include <metal_stdlib>
#include "shader_types.h"
#include "voxel_grid.h"
using namespace metal;

// One thread per column: sample cascade displacement, quantize (floor
// policy), write the per-column water surface state. Mirror of
// src/voxel/VoxelWorld — keep the math in lockstep.
kernel void world_fill(
    constant WorldFillUniforms& U                      [[buffer(0)]],
    texture2d<float, access::write> surface            [[texture(0)]],
    array<texture2d<float>, MAX_CASCADES> disp_tex     [[texture(1)]],
    array<texture2d<float>, MAX_CASCADES> normal_tex   [[texture(1 + MAX_CASCADES)]],
    texture2d<float, access::read> ripple              [[texture(1 + 2 * MAX_CASCADES)]],
    uint2 gid [[thread_position_in_grid]])
{
    if ((int)gid.x >= U.grid_extent || (int)gid.y >= U.grid_extent) return;
    VoxelGridDesc g = {U.grid_extent, U.height_cells, U.voxel_size_m, U.height_step_m, U.base_depth_m};
    constexpr sampler smp(filter::linear, address::repeat);

    float half_patch = vg_half_patch(g);
    float2 xz = (float2(gid) + 0.5) * U.voxel_size_m - half_patch;

    float h = 0.0;
    float fold_min = 1.0;
    int n = min(U.cascade_count, MAX_CASCADES);
    for (int i = 0; i < n; ++i) {
        float2 uv = xz / U.cascade_size[i];
        // disp layout (post_fft.metal): .y = vertical h; horizontal
        // choppiness ignored — voxel columns are axis-aligned.
        h += disp_tex[i].sample(smp, uv, level(0)).y;
        // level(0): one canonical sample per column.
        fold_min = min(fold_min, normal_tex[i].sample(smp, uv, level(0)).w);
    }
    // Interactive ripple layer: summed with the FFT before quantization.
    float ripple_h = ripple.read(gid).x;
    h += ripple_h;
    // Mirror of VoxelWorld::quantize_height (floor policy).
    float top = vg_quantize_height(g, h);

    // Per-column water surface height + folding for the marcher's shading.
    // Ripple amplitude lowers fold_min so impacts read as foam in the marcher.
    surface.write(float4(top, fold_min - U.ripple_foam * abs(ripple_h), 0.0, 0.0), gid);
}
