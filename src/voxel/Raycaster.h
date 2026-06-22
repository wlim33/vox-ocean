#pragma once
#include "voxel/VoxelWorld.h"
#include <glm/glm.hpp>
#include <optional>
#include <cstdint>

namespace vox {

// Result of a screen-space pick against the CPU voxel grid.
struct PickHit {
    int      ix = -1, iy = -1, iz = -1;  // hit cell
    uint32_t linear_idx = 0;             // vg_cell_index(ix, iy, iz)
    uint8_t  material = 0;               // VoxMat at the hit cell
    int      face_axis = -1;             // face the ray entered: 0=x 1=y 2=z
    float    t = 0.0f;                   // ray parameter at the hit
    // Build target: the empty cell adjacent to the entered face (the cell the ray
    // was in just before the hit). has_neighbor=false at the grid edge.
    bool     has_neighbor = false;
    int      nx = -1, ny = -1, nz = -1;
    uint32_t neighbor_idx = 0;
};

// Empty cell adjacent to the entered face. The ray (direction `dir`) enters cell
// (ix,iy,iz) through `face_axis`; the previous cell along the ray is one step back
// on that axis. has_neighbor=false if that cell is outside the grid or face_axis<0.
struct FaceNeighbor { bool has_neighbor = false; int nx = -1, ny = -1, nz = -1; uint32_t idx = 0; };
FaceNeighbor face_neighbor(const VoxelWorld& grid, int ix, int iy, int iz,
                           int face_axis, const glm::vec3& dir);

// Pixel -> world ray -> first non-air cell. pixel_x/pixel_y are drawable pixels,
// origin top-left. Mirrors the PERSPECTIVE branch of the shader's ray-gen
// (shaders/voxel_march.metal:104-110): the ray starts at camera_pos. The shader's
// orthographic backup (ortho_backup > 0) is intentionally NOT handled here -- pick()
// is for the interactive perspective camera only. Returns nullopt on a degenerate
// viewport or a miss.
std::optional<PickHit> pick(int viewport_w, int viewport_h,
                            float pixel_x, float pixel_y,
                            const glm::mat4& inv_view_proj,
                            const glm::vec3& camera_pos,
                            const VoxelWorld& grid,
                            const uint8_t* materials,
                            int max_steps);

}  // namespace vox
