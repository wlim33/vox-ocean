#include "voxel/MaterialCa.h"
#include "voxel/VoxelWorld.h"   // VoxMat
#include <algorithm>
namespace vox {

void resolve_block(uint8_t cls[8]) {
    // local index = lx + 2*ly + 4*lz ; ly=0 is the lower level.
    // 1. Straight fall: upper SAND drops into the EMPTY directly below.
    for (int lz = 0; lz < 2; ++lz)
        for (int lx = 0; lx < 2; ++lx) {
            int lo = lx + 4 * lz, up = lo + 2;
            if (cls[up] == CA_SAND && cls[lo] == CA_EMPTY) { cls[lo] = CA_SAND; cls[up] = CA_EMPTY; }
        }
    // 2. Diagonal spread (angle of repose): an upper SAND still resting on a
    //    non-empty cell slides into an EMPTY lower cell elsewhere in the block.
    //    Fixed order (x before z) keeps it deterministic and single-target.
    for (int lz = 0; lz < 2; ++lz)
        for (int lx = 0; lx < 2; ++lx) {
            int up = lx + 2 + 4 * lz;
            if (cls[up] != CA_SAND) continue;
            int dx = (1 - lx) + 4 * lz;     // ly=0, opposite x
            int dz = lx + 4 * (1 - lz);     // ly=0, opposite z
            if      (cls[dx] == CA_EMPTY) { cls[dx] = CA_SAND; cls[up] = CA_EMPTY; }
            else if (cls[dz] == CA_EMPTY) { cls[dz] = CA_SAND; cls[up] = CA_EMPTY; }
        }
}

namespace {
// Classify a world cell into {EMPTY, SAND, BARRIER}. Out-of-grid is BARRIER.
// Sand below its column's terrain_top is static terrain → BARRIER; at/above is dynamic.
inline uint8_t classify(const std::vector<uint8_t>& cells, const MaterialCaDims& d,
                        const std::vector<uint8_t>& terrain_top, int ix, int iy, int iz) {
    if (ix < 0 || ix >= d.extent || iy < 0 || iy >= d.height_cells || iz < 0 || iz >= d.extent)
        return CA_BARRIER;
    uint8_t m = cells[ca_cell_index(d, ix, iy, iz)];
    if (m == (uint8_t)VoxMat::Air) return CA_EMPTY;
    if (m == (uint8_t)VoxMat::Sand && iy >= terrain_top[(size_t)iz * d.extent + ix]) return CA_SAND;
    return CA_BARRIER;
}
}

void margolus_sweep(std::vector<uint8_t>& cells, const MaterialCaDims& d,
                    const std::vector<uint8_t>& terrain_top,
                    int ox, int oy, int oz,
                    int x0, int y0, int z0, int x1, int y1, int z1,
                    std::vector<uint32_t>& changed) {
    // Block lower corners are at coord ≡ offset (mod 2). Start one step below the
    // box so a partial edge block that owns an in-range cell is still visited.
    int bx0 = x0 - ((x0 - ox) & 1), by0 = y0 - ((y0 - oy) & 1), bz0 = z0 - ((z0 - oz) & 1);
    for (int bz = bz0; bz <= z1; bz += 2)
        for (int by = by0; by <= y1; by += 2)
            for (int bx = bx0; bx <= x1; bx += 2) {
                uint8_t cls[8], before[8];
                for (int lz = 0; lz < 2; ++lz)
                    for (int ly = 0; ly < 2; ++ly)
                        for (int lx = 0; lx < 2; ++lx) {
                            uint8_t c = classify(cells, d, terrain_top, bx + lx, by + ly, bz + lz);
                            cls[lx + 2 * ly + 4 * lz] = c;
                            before[lx + 2 * ly + 4 * lz] = c;
                        }
                resolve_block(cls);
                for (int lz = 0; lz < 2; ++lz)
                    for (int ly = 0; ly < 2; ++ly)
                        for (int lx = 0; lx < 2; ++lx) {
                            int l = lx + 2 * ly + 4 * lz;
                            if (cls[l] == before[l]) continue;
                            int wx = bx + lx, wy = by + ly, wz = bz + lz;
                            if (wx < 0 || wx >= d.extent || wy < 0 || wy >= d.height_cells ||
                                wz < 0 || wz >= d.extent) continue;            // never write OOB
                            int idx = ca_cell_index(d, wx, wy, wz);
                            cells[idx] = (cls[l] == CA_SAND) ? (uint8_t)VoxMat::Sand
                                                            : (uint8_t)VoxMat::Air;
                            changed.push_back((uint32_t)idx);
                        }
            }
}
}
