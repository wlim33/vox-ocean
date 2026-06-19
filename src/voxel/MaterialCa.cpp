#include "voxel/MaterialCa.h"
#include "voxel/VoxelWorld.h"   // VoxMat
#include "voxel/MaterialRegistry.h"
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
// Classify a world cell into {EMPTY, SAND, BARRIER} by material phase.
// Out-of-grid is BARRIER. Granular→CA_SAND; Empty→CA_EMPTY; Solid→CA_BARRIER.
inline uint8_t classify(const std::vector<uint8_t>& cells, const MaterialCaDims& d,
                        int ix, int iy, int iz) {
    if (ix < 0 || ix >= d.extent || iy < 0 || iy >= d.height_cells || iz < 0 || iz >= d.extent)
        return CA_BARRIER;
    switch (material_props((VoxMat)cells[ca_cell_index(d, ix, iy, iz)]).phase) {
        case Phase::Empty:    return CA_EMPTY;
        case Phase::Granular: return CA_SAND;
        case Phase::Solid:    return CA_BARRIER;
    }
    return CA_BARRIER;
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
                uint8_t cls[8], before[8], mat[8];
                uint8_t granular_id = (uint8_t)VoxMat::Air;   // id of the dynamic grain in this block
                for (int lz = 0; lz < 2; ++lz)
                    for (int ly = 0; ly < 2; ++ly)
                        for (int lx = 0; lx < 2; ++lx) {
                            int wx = bx + lx, wy = by + ly, wz = bz + lz;
                            bool in = wx >= 0 && wx < d.extent && wy >= 0 && wy < d.height_cells &&
                                      wz >= 0 && wz < d.extent;
                            uint8_t m = in ? cells[ca_cell_index(d, wx, wy, wz)] : (uint8_t)VoxMat::Air;
                            uint8_t c = classify(cells, d, wx, wy, wz);
                            int l = lx + 2 * ly + 4 * lz;
                            cls[l] = before[l] = c;
                            mat[l] = m;
                            if (c == CA_SAND) granular_id = m;   // single dynamic id per block in SP1
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
                            cells[idx] = (cls[l] == CA_SAND) ? granular_id : (uint8_t)VoxMat::Air;
                            changed.push_back((uint32_t)idx);
                        }
            }
}
}
