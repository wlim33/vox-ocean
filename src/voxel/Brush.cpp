#include "voxel/Brush.h"

namespace vox {
void sphere_cells(const VoxelWorld& grid, uint32_t center, int radius,
                  std::vector<uint32_t>& out) {
    out.clear();
    int cx, cy, cz;
    grid.decode_cell_index((int)center, cx, cy, cz);
    const int E  = grid.params().extent;
    const int H  = grid.params().height_cells;
    const int r2 = radius * radius;
    for (int dz = -radius; dz <= radius; ++dz)
        for (int dy = -radius; dy <= radius; ++dy)
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx * dx + dy * dy + dz * dz > r2) continue;
                int x = cx + dx, y = cy + dy, z = cz + dz;
                if (x < 0 || x >= E || y < 0 || y >= H || z < 0 || z >= E) continue;
                out.push_back((uint32_t)grid.cell_index(x, y, z));
            }
}
}
