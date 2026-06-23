#pragma once
#include "voxel/VoxelWorld.h"
#include <vector>
#include <cstdint>

namespace vox {
// All in-bounds grid cells whose center is within `radius` cells (Euclidean) of
// `center`'s cell: included iff dx*dx + dy*dy + dz*dz <= radius*radius. radius 0
// -> {center}. Cells outside the grid are skipped (clamped at edges). `out` is
// cleared, then filled. No duplicates.
void sphere_cells(const VoxelWorld& grid, uint32_t center, int radius,
                  std::vector<uint32_t>& out);
}
