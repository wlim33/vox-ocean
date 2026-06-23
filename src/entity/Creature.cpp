// src/entity/Creature.cpp
#include "entity/Creature.h"
#include "entity/CreatureRegistry.h"
#include "world/World.h"
#include "core/Config.h"
#include <cmath>
namespace vox {
namespace {
// World (x,y,z) -> grid cell indices. Returns false if outside the grid.
bool world_to_cell(const VoxelWorld& g, float x, float y, float z,
                   int& ix, int& iy, int& iz) {
    const auto& p = g.params();
    float half = 0.5f * p.extent * p.voxel_size_m;
    ix = (int)std::floor((x + half) / p.voxel_size_m);
    iz = (int)std::floor((z + half) / p.voxel_size_m);
    iy = (int)std::floor((y + p.base_depth_m) / p.height_step_m);
    return ix >= 0 && ix < p.extent && iz >= 0 && iz < p.extent
        && iy >= 0 && iy < p.height_cells;
}
}

VoxMat CreatureCtx::sample(float x, float y, float z) const {
    int ix, iy, iz;
    if (!world_to_cell(grid, x, y, z, ix, iy, iz)) return VoxMat::Air;
    return (VoxMat)world.material()[grid.cell_index(ix, iy, iz)];
}

float CreatureCtx::floor_top_y(float x, float z) const {
    return world.floor_top_y(x, z);
}

std::optional<glm::vec3> CreatureCtx::find_nearest(
    glm::vec3 from, float radius,
    const std::function<bool(VoxMat)>& pred) const {
    const auto& p = grid.params();
    float half = 0.5f * p.extent * p.voxel_size_m;
    // Cell-index bounds around the query sphere.
    auto clampi = [](int v, int lo, int hi){ return v < lo ? lo : (v > hi ? hi : v); };
    int ix0 = clampi((int)std::floor((from.x - radius + half) / p.voxel_size_m), 0, p.extent - 1);
    int ix1 = clampi((int)std::floor((from.x + radius + half) / p.voxel_size_m), 0, p.extent - 1);
    int iz0 = clampi((int)std::floor((from.z - radius + half) / p.voxel_size_m), 0, p.extent - 1);
    int iz1 = clampi((int)std::floor((from.z + radius + half) / p.voxel_size_m), 0, p.extent - 1);
    int iy0 = clampi((int)std::floor((from.y - radius + p.base_depth_m) / p.height_step_m), 0, p.height_cells - 1);
    int iy1 = clampi((int)std::floor((from.y + radius + p.base_depth_m) / p.height_step_m), 0, p.height_cells - 1);
    const float r2 = radius * radius;
    bool found = false;
    float best2 = r2;
    glm::vec3 best{0.0f};
    const auto& mats = world.material();
    // Iterate in ascending cell-index order (x fastest, then y, then z) so ties
    // resolve to the lowest index deterministically.
    for (int iz = iz0; iz <= iz1; ++iz)
        for (int iy = iy0; iy <= iy1; ++iy)
            for (int ix = ix0; ix <= ix1; ++ix) {
                if (!pred((VoxMat)mats[grid.cell_index(ix, iy, iz)])) continue;
                float cx = grid.column_center_x(ix);
                float cz = grid.column_center_z(iz);
                float cy = grid.cell_bottom_y(iy) + 0.5f * p.height_step_m;
                float dx = cx - from.x, dy = cy - from.y, dz = cz - from.z;
                float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 < best2) { best2 = d2; best = {cx, cy, cz}; found = true; }
            }
    if (found) return best;
    return std::nullopt;
}

void CreatureCtx::for_each_neighbor(
    glm::vec3 from, float radius,
    const std::function<void(const CreaturePresence&)>& fn) const {
    neighbors.for_each(from, radius, fn);
}

}
