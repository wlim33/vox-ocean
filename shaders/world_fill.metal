#include <metal_stdlib>
#include "shader_types.h"
#include "voxel_grid.h"
using namespace metal;

// One thread per column: sample cascade displacement, quantize (floor
// policy), write the full voxel column — terrain wins over water, so dunes
// can break the surface as islands. Mirror of src/voxel/VoxelWorld — keep
// the math in lockstep.
kernel void world_fill(
    constant WorldFillUniforms& U                      [[buffer(0)]],
    texture3d<uint, access::read>   terrain            [[texture(0)]],
    texture3d<uint, access::write>  world              [[texture(1)]],
    texture2d<float, access::write> surface            [[texture(2)]],
    array<texture2d<float>, MAX_CASCADES> disp_tex     [[texture(3)]],
    array<texture2d<float>, MAX_CASCADES> normal_tex   [[texture(3 + MAX_CASCADES)]],
    texture2d<float, access::read> ripple              [[texture(3 + 2 * MAX_CASCADES)]],
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
    // Mirror of VoxelWorld::quantize_height / water_top_cell (floor policy).
    float top = vg_quantize_height(g, h);
    int water_cells = vg_water_top_cell(g, h);

    for (int iy = 0; iy < U.height_cells; ++iy) {
        uint t = terrain.read(uint3(gid.x, iy, gid.y)).r;
        uint m = (t != MAT_AIR) ? t : (iy < water_cells ? MAT_WATER : MAT_AIR);
        world.write(uint4(m, 0, 0, 0), uint3(gid.x, iy, gid.y));
    }
    // Per-column water surface height + folding for the marcher's shading.
    // Ripple amplitude lowers fold_min so impacts read as foam in the marcher.
    surface.write(float4(top, fold_min - U.ripple_foam * abs(ripple_h), 0.0, 0.0), gid);
}

// Persistent grid: rewrite only the surface band [min(prev,cur),max(prev,cur)) per
// column, plus seed full columns on the first frame (prev < 0). Mirror of world_fill.
kernel void world_fill_incremental(
    constant WorldFillUniforms& U                      [[buffer(0)]],
    device int* prev_water                             [[buffer(1)]],
    texture3d<uint, access::read>   terrain            [[texture(0)]],
    texture3d<uint, access::write>  world              [[texture(1)]],
    texture2d<float, access::write> surface            [[texture(2)]],
    array<texture2d<float>, MAX_CASCADES> disp_tex     [[texture(3)]],
    array<texture2d<float>, MAX_CASCADES> normal_tex   [[texture(3 + MAX_CASCADES)]],
    texture2d<float, access::read> ripple              [[texture(3 + 2 * MAX_CASCADES)]],
    uint2 gid [[thread_position_in_grid]])
{
    if ((int)gid.x >= U.grid_extent || (int)gid.y >= U.grid_extent) return;
    VoxelGridDesc g = {U.grid_extent, U.height_cells, U.voxel_size_m, U.height_step_m, U.base_depth_m};
    constexpr sampler smp(filter::linear, address::repeat);
    float half_patch = vg_half_patch(g);
    float2 xz = (float2(gid) + 0.5) * U.voxel_size_m - half_patch;
    float h = 0.0, fold_min = 1.0;
    int n = min(U.cascade_count, MAX_CASCADES);
    for (int i = 0; i < n; ++i) {
        float2 uv = xz / U.cascade_size[i];
        h += disp_tex[i].sample(smp, uv, level(0)).y;
        fold_min = min(fold_min, normal_tex[i].sample(smp, uv, level(0)).w);
    }
    float ripple_h = ripple.read(gid).x;
    h += ripple_h;
    float top = vg_quantize_height(g, h);
    int cur = vg_water_top_cell(g, h);

    int col = (int)gid.y * U.grid_extent + (int)gid.x;
    int prev = prev_water[col];
    int lo = (prev < 0) ? 0 : min(prev, cur);
    int hi = (prev < 0) ? U.height_cells : max(prev, cur);
    for (int iy = lo; iy < hi; ++iy) {
        uint t = terrain.read(uint3(gid.x, iy, gid.y)).r;
        uint m = (t != MAT_AIR) ? t : (iy < cur ? MAT_WATER : MAT_AIR);
        world.write(uint4(m, 0, 0, 0), uint3(gid.x, iy, gid.y));
    }
    prev_water[col] = cur;
    surface.write(float4(top, fold_min - U.ripple_foam * abs(ripple_h), 0.0, 0.0), gid);
}

// Dev verify: count cells where the live grid and the full-rebuild scratch differ.
kernel void grid_diff(
    constant WorldFillUniforms& U          [[buffer(0)]],
    device atomic_uint* mismatch           [[buffer(1)]],
    texture3d<uint, access::read> a        [[texture(0)]],
    texture3d<uint, access::read> b        [[texture(1)]],
    uint3 gid [[thread_position_in_grid]])
{
    if ((int)gid.x >= U.grid_extent || (int)gid.y >= U.height_cells || (int)gid.z >= U.grid_extent) return;
    if (a.read(gid).r != b.read(gid).r)
        atomic_fetch_add_explicit(mismatch, 1u, memory_order_relaxed);
}
