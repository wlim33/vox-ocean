#include "voxel/MaterialCa.h"
#include "voxel/VoxelWorld.h"   // VoxMat
#include "voxel/MaterialRegistry.h"
#include <algorithm>
namespace vox {

namespace {
inline float ca_density(uint8_t m) { return material_props((VoxMat)m).density; }
inline bool  ca_movable(uint8_t m) { return material_props((VoxMat)m).movable; }
// fluidity >= 0.5 -> levels laterally (liquid); < 0.5 -> repose (granular).
inline bool  ca_levels (uint8_t m) { return material_props((VoxMat)m).fluidity >= 0.5f; }
// `a` may displace `b` only if both are movable and a is strictly denser than b.
inline bool  can_sink  (uint8_t a, uint8_t b) {
    return ca_movable(a) && ca_movable(b) && ca_density(a) > ca_density(b);
}
}

// Density-ordered settle within one 2x2x2 block; ids in `mat`, local = lx+2*ly+4*lz, ly=0 lower.
// This is a permutation: only std::swap is used, so counts are always conserved.
void resolve_block(uint8_t mat[8]) {
    // 1. Vertical: heavier of each vertical pair sinks.
    for (int lz = 0; lz < 2; ++lz)
        for (int lx = 0; lx < 2; ++lx) {
            int lo = lx + 4 * lz, up = lo + 2;
            if (can_sink(mat[up], mat[lo])) std::swap(mat[up], mat[lo]);
        }
    // 2. Lateral: an un-sunk movable cell spreads toward a lighter lower cell.
    //    Granular -> only diagonally DOWN (repose). Liquid (levels) -> also same level.
    //    Fixed (x before z) order is deterministic; each cell may move at most once
    //    (continue after any swap keeps it single-target).
    for (int lz = 0; lz < 2; ++lz)
        for (int lx = 0; lx < 2; ++lx) {
            int up = lx + 2 + 4 * lz;
            if (!ca_movable(mat[up])) continue;
            int dx = (1 - lx) + 4 * lz;        // ly=0, opposite x
            int dz = lx + 4 * (1 - lz);        // ly=0, opposite z
            if      (can_sink(mat[up], mat[dx])) { std::swap(mat[up], mat[dx]); continue; }
            else if (can_sink(mat[up], mat[dz])) { std::swap(mat[up], mat[dz]); continue; }
            if (ca_levels(mat[up])) {
                int sx = (1 - lx) + 2 + 4 * lz; // ly=1, opposite x
                int sz = lx + 2 + 4 * (1 - lz); // ly=1, opposite z
                if      (can_sink(mat[up], mat[sx])) { std::swap(mat[up], mat[sx]); continue; }
                else if (can_sink(mat[up], mat[sz])) { std::swap(mat[up], mat[sz]); continue; }
            }
        }
}

void MaterialCa::wake_box(int x0, int y0, int z0, int x1, int y1, int z1) {
    if (!awake()) { ax0_ = x0; ay0_ = y0; az0_ = z0; ax1_ = x1; ay1_ = y1; az1_ = z1; return; }
    ax0_ = std::min(ax0_, x0); ay0_ = std::min(ay0_, y0); az0_ = std::min(az0_, z0);
    ax1_ = std::max(ax1_, x1); ay1_ = std::max(ay1_, y1); az1_ = std::max(az1_, z1);
}

void MaterialCa::step(std::vector<uint8_t>& cells, const MaterialCaDims& d,
                      std::vector<uint32_t>& changed) {
    if (!awake()) return;
    // Phase schedule: alternate oy every step (continuous fall); cycle ox,oz so
    // piles can spread. 4-phase deterministic sequence keyed off the step counter.
    static const int OX[4] = {0, 1, 0, 1}, OY[4] = {0, 1, 0, 1}, OZ[4] = {0, 0, 1, 1};
    int p = phase_ & 3;
    int x0 = ax0_, y0 = ay0_, z0 = az0_, x1 = ax1_, y1 = ay1_, z1 = az1_;
    margolus_sweep(cells, d, OX[p], OY[p], OZ[p], x0, y0, z0, x1, y1, z1, changed);
    ++phase_;
    // A single no-motion phase is ambiguous: under the Margolus partition a grain
    // can be the lower cell of its block this phase (so it cannot move) yet fall on
    // the next phase once the origin shifts. So sleep only after a FULL phase cycle
    // produced no motion; until then keep the active box so the next phase can act.
    // (Callers pass a fresh `changed` per step, so empty == no motion this phase.)
    if (changed.empty()) {
        if (++quiet_ >= 4) clear_box();
        return;
    }
    quiet_ = 0;
    // Re-derive the active box from what moved (±1 so the falling front and
    // newly-exposed neighbours stay awake).
    int nx0 = d.extent, ny0 = d.height_cells, nz0 = d.extent, nx1 = -1, ny1 = -1, nz1 = -1;
    for (uint32_t idx : changed) {
        int ix = (int)(idx % d.extent);
        int iy = (int)((idx / d.extent) % d.height_cells);
        int iz = (int)(idx / ((size_t)d.extent * d.height_cells));
        nx0 = std::min(nx0, ix - 1); nx1 = std::max(nx1, ix + 1);
        ny0 = std::min(ny0, iy - 1); ny1 = std::max(ny1, iy + 1);
        nz0 = std::min(nz0, iz - 1); nz1 = std::max(nz1, iz + 1);
    }
    ax0_ = std::max(0, nx0); ay0_ = std::max(0, ny0); az0_ = std::max(0, nz0);
    ax1_ = std::min(d.extent - 1, nx1); ay1_ = std::min(d.height_cells - 1, ny1);
    az1_ = std::min(d.extent - 1, nz1);
}

void margolus_sweep(std::vector<uint8_t>& cells, const MaterialCaDims& d,
                    int ox, int oy, int oz,
                    int x0, int y0, int z0, int x1, int y1, int z1,
                    std::vector<uint32_t>& changed) {
    // Block lower corners are at coord ≡ offset (mod 2). Start one step below the
    // box so a partial edge block that owns an in-range cell is still visited.
    int bx0 = x0 - ((x0 - ox) & 1), by0 = y0 - ((y0 - oy) & 1), bz0 = z0 - ((z0 - oz) & 1);
    for (int bz = bz0; bz <= z1; bz += 2)
        for (int by = by0; by <= y1; by += 2)
            for (int bx = bx0; bx <= x1; bx += 2) {
                uint8_t mat[8], before[8];
                for (int lz = 0; lz < 2; ++lz)
                    for (int ly = 0; ly < 2; ++ly)
                        for (int lx = 0; lx < 2; ++lx) {
                            int wx = bx + lx, wy = by + ly, wz = bz + lz;
                            bool in = wx >= 0 && wx < d.extent && wy >= 0 && wy < d.height_cells &&
                                      wz >= 0 && wz < d.extent;
                            // Out-of-grid reads as an immovable wall: use Rock (movable=false).
                            uint8_t m = in ? cells[ca_cell_index(d, wx, wy, wz)] : (uint8_t)VoxMat::Rock;
                            int l = lx + 2 * ly + 4 * lz;
                            mat[l] = before[l] = m;
                        }
                resolve_block(mat);
                for (int lz = 0; lz < 2; ++lz)
                    for (int ly = 0; ly < 2; ++ly)
                        for (int lx = 0; lx < 2; ++lx) {
                            int l = lx + 2 * ly + 4 * lz;
                            if (mat[l] == before[l]) continue;
                            int wx = bx + lx, wy = by + ly, wz = bz + lz;
                            if (wx < 0 || wx >= d.extent || wy < 0 || wy >= d.height_cells ||
                                wz < 0 || wz >= d.extent) continue;     // never write OOB
                            int idx = ca_cell_index(d, wx, wy, wz);
                            cells[idx] = mat[l];                        // multi-material identity
                            changed.push_back((uint32_t)idx);
                        }
            }
}
}
