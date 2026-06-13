#pragma once
#include <cassert>
#include <cmath>
#include <cstdint>
namespace vox {

// Material IDs stored in the world grid (one byte per cell).
// Lockstep mirror of the MAT_* constants in shaders/shader_types.h
// (static_asserted in voxel_world_test.cpp).
enum class VoxMat : uint8_t { Air = 0, Water = 1, Sand = 2, Rock = 3, Boat = 4, Kelp = 5, Fish = 6 };

// extent = columns per axis; height_cells = vertical cells above the diorama
// base. Cells are voxel_size_m wide and height_step_m tall (non-cubic OK).
// Preconditions: all fields > 0 (Config layer clamps upstream).
// Invariant: base_depth_m must be an integer multiple of height_step_m, so
// cell boundaries align with quantized heights (asserted in the ctor).
struct VoxelWorldParams {
    int   extent;
    int   height_cells;
    float voxel_size_m;
    float height_step_m;
    float base_depth_m;
};

// CPU mirror of the grid math in shaders/world_fill.metal and
// shaders/voxel_march.metal — keep in lockstep.
class VoxelWorld {
public:
    explicit VoxelWorld(VoxelWorldParams p) : p_(p) {
        assert(p_.extent > 0 && p_.height_cells > 0);
        assert(p_.voxel_size_m > 0.0f && p_.height_step_m > 0.0f);
        // base must sit on a step boundary or fill and march desync at the surface.
        float steps = p_.base_depth_m / p_.height_step_m;
        assert(std::abs(steps - std::round(steps)) < 1e-4f * steps);
    }
    int   columns() const { return p_.extent * p_.extent; }
    int   cells()   const { return columns() * p_.height_cells; }
    float patch_size_m() const { return p_.extent * p_.voxel_size_m; }
    float world_top_y()  const { return -p_.base_depth_m + p_.height_cells * p_.height_step_m; }
    float column_center_x(int ix) const;
    float column_center_z(int iz) const { return column_center_x(iz); }
    float cell_bottom_y(int iy) const { return -p_.base_depth_m + iy * p_.height_step_m; }
    // Floor quantization policy (anti-flicker, user decision).
    float quantize_height(float h) const;
    // Cells, counted up from the diorama base, that a water column of
    // (unquantized) surface height h fills. Clamped to [1, height_cells].
    int   water_top_cell(float h) const;
    // Linear index matching the 3D-texture upload layout: x fastest, then y,
    // then z (bytesPerRow = extent, bytesPerImage = extent * height_cells).
    int   cell_index(int ix, int iy, int iz) const {
        return (iz * p_.height_cells + iy) * p_.extent + ix;
    }
    // Inverse of cell_index — lockstep mirror of the decode in
    // shaders/stamp.metal's stamp_cells kernel.
    void decode_cell_index(int i, int& ix, int& iy, int& iz) const {
        ix = i % p_.extent;
        iy = (i / p_.extent) % p_.height_cells;
        iz = i / (p_.extent * p_.height_cells);
    }
    const VoxelWorldParams& params() const { return p_; }
private:
    VoxelWorldParams p_;
};
}
