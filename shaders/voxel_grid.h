#pragma once
// Single source of truth for voxel-grid math, included by BOTH C++ and Metal
// (mirrors the shared-header pattern in fft_common.h / shader_types.h). Replaces
// the grid math previously duplicated across VoxelWorld, Dda.cpp, world_fill.metal,
// voxel_march.metal, and apply_edits.metal. Tests: tests/voxel_grid_test.cpp.

#ifdef __METAL_VERSION__
  #define VG_FLOOR(x)         floor(x)
  #define VG_MAXF(a, b)       max((float)(a), (float)(b))
  #define VG_CLAMPI(v, lo, hi) clamp((int)(v), (int)(lo), (int)(hi))
#else
  #include <cmath>
  #include <algorithm>
  #define VG_FLOOR(x)         std::floor(x)
  #define VG_MAXF(a, b)       std::max((float)(a), (float)(b))
  #define VG_CLAMPI(v, lo, hi) std::clamp((int)(v), (int)(lo), (int)(hi))
#endif

struct VoxelGridDesc {
    int   extent;        // columns per axis (x and z)
    int   height_cells;  // vertical cells above the diorama base
    float voxel_size_m;  // cell width (x, z)
    float height_step_m; // cell height (y)
    float base_depth_m;  // diorama floor below y = 0
};

struct VgCell { int ix, iy, iz; };
struct VgCol  { int ix, iz; };

// Linear index for the terrain staging upload + the edit kernel: x fastest, then y, then z.
inline int vg_cell_index(VoxelGridDesc g, int ix, int iy, int iz) {
    return (iz * g.height_cells + iy) * g.extent + ix;
}
inline VgCell vg_decode_index(VoxelGridDesc g, int i) {
    VgCell c;
    c.ix =  i % g.extent;
    c.iy = (i / g.extent) % g.height_cells;
    c.iz =  i / (g.extent * g.height_cells);
    return c;
}
// Half the diorama side length (grid is centered on the origin).
inline float vg_half_patch(VoxelGridDesc g) { return 0.5f * (float)g.extent * g.voxel_size_m; }
inline float vg_world_top_y(VoxelGridDesc g) { return -g.base_depth_m + (float)g.height_cells * g.height_step_m; }
// World center of column i (x or z).
inline float vg_column_center(VoxelGridDesc g, int i) { return ((float)i + 0.5f) * g.voxel_size_m - vg_half_patch(g); }
// Floor-quantize a surface height to a step boundary (anti-flicker), clamped one step above base.
inline float vg_quantize_height(VoxelGridDesc g, float h) {
    float q = VG_FLOOR(h / g.height_step_m) * g.height_step_m;
    return VG_MAXF(q, -g.base_depth_m + g.height_step_m);
}
// Water cells from the base for an ALREADY-quantized surface top (world-y).
// This is the second half of vg_water_top_cell, usable when only the quantized
// top is available (e.g. the marcher reading surface_tex_.x). For any h,
// vg_water_cells_from_top(g, vg_quantize_height(g, h)) == vg_water_top_cell(g, h).
inline int vg_water_cells_from_top(VoxelGridDesc g, float top) {
    int n = (int)VG_FLOOR((top + g.base_depth_m) / g.height_step_m + 0.5f);
    return VG_CLAMPI(n, 1, g.height_cells);
}
// Water cells from the base for surface height h; clamped [1, height_cells].
inline int vg_water_top_cell(VoxelGridDesc g, float h) {
    return vg_water_cells_from_top(g, vg_quantize_height(g, h));
}
// World (x, z) -> clamped column indices (the lookup behind surface height_at).
inline VgCol vg_column_at(VoxelGridDesc g, float x, float z) {
    float half_ext = vg_half_patch(g);
    VgCol c;
    c.ix = VG_CLAMPI((int)VG_FLOOR((x + half_ext) / g.voxel_size_m), 0, g.extent - 1);
    c.iz = VG_CLAMPI((int)VG_FLOOR((z + half_ext) / g.voxel_size_m), 0, g.extent - 1);
    return c;
}
