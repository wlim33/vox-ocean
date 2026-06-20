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
                // Guard: only allow a same-level lateral move when the cell BELOW
                // the target is also lighter than `up`.  dx is the cell at ly=0
                // directly below sx (opposite-x); dz is below sz (opposite-z).
                // Without this, a 1-cell water bump at the flat sea surface slides
                // same-level into adjacent Air even though there is no genuine
                // downhill, causing a Water+Air pair to ping-pong indefinitely and
                // preventing the CA from sleeping on a calm ocean.
                if      (can_sink(mat[up], mat[sx]) && can_sink(mat[up], mat[dx])) { std::swap(mat[up], mat[sx]); continue; }
                else if (can_sink(mat[up], mat[sz]) && can_sink(mat[up], mat[dz])) { std::swap(mat[up], mat[sz]); continue; }
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
    if (combustion_)
        combustion_sweep(cells, d, (uint32_t)phase_, seed_, cparams_,
                         ax0_, ay0_, az0_, ax1_, ay1_, az1_, changed);
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
    // When combustion is enabled, reactive materials (Fire, Smoke) in the active box
    // will eventually change but may not on any given step due to stochastic rates.
    // Scan the box so a lucky run of no-RNG-fires doesn't prematurely sleep the CA.
    bool reactive_present = false;
    if (combustion_ && changed.empty()) {
        const uint8_t kFire  = (uint8_t)VoxMat::Fire;
        const uint8_t kSmoke = (uint8_t)VoxMat::Smoke;
        for (int iz = az0_; iz <= az1_ && !reactive_present; ++iz)
            for (int iy = ay0_; iy <= ay1_ && !reactive_present; ++iy)
                for (int ix = ax0_; ix <= ax1_ && !reactive_present; ++ix) {
                    uint8_t m = cells[ca_cell_index(d, ix, iy, iz)];
                    if (m == kFire || m == kSmoke) reactive_present = true;
                }
    }
    if (changed.empty() && !reactive_present) {
        if (++quiet_ >= 4) clear_box();
        return;
    }
    quiet_ = 0;
    if (changed.empty()) return;   // reactive_present but no movement: keep current box
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
    // When combustion is active, scan the OLD box for Fire/Smoke that may not have
    // generated changes this step (stochastic rates) and fold them into the new box
    // so they remain covered on future steps.
    if (combustion_) {
        const uint8_t kFire  = (uint8_t)VoxMat::Fire;
        const uint8_t kSmoke = (uint8_t)VoxMat::Smoke;
        for (int iz = z0; iz <= z1; ++iz)
            for (int iy = y0; iy <= y1; ++iy)
                for (int ix = x0; ix <= x1; ++ix) {
                    uint8_t m = cells[ca_cell_index(d, ix, iy, iz)];
                    if (m == kFire || m == kSmoke) {
                        nx0 = std::min(nx0, ix - 1); nx1 = std::max(nx1, ix + 1);
                        ny0 = std::min(ny0, iy - 1); ny1 = std::max(ny1, iy + 1);
                        nz0 = std::min(nz0, iz - 1); nz1 = std::max(nz1, iz + 1);
                    }
                }
    }
    ax0_ = std::max(0, nx0); ay0_ = std::max(0, ny0); az0_ = std::max(0, nz0);
    ax1_ = std::min(d.extent - 1, nx1); ay1_ = std::min(d.height_cells - 1, ny1);
    az1_ = std::min(d.extent - 1, nz1);
}

namespace {
// Deterministic 32-bit mix (wang/murmur-style).
inline uint32_t mix32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x;
}
// Uniform [0,1) from cell coords, step, world seed, and a per-event salt (so
// independent events at the same cell/step do not correlate).
inline float rnd01(int x, int y, int z, uint32_t step, uint32_t seed, uint32_t salt) {
    uint32_t h = ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u) ^ ((uint32_t)z * 83492791u)
               ^ (step * 2654435761u) ^ seed ^ (salt * 2246822519u);
    return (mix32(h) >> 8) * (1.0f / 16777216.0f);   // 24-bit mantissa -> [0,1)
}
inline bool is_fuel(uint8_t m) { return material_props((VoxMat)m).flammability > 0.0f; }
}

void combustion_sweep(std::vector<uint8_t>& cells, const MaterialCaDims& d,
                      uint32_t step, uint32_t seed, const CombustionParams& p,
                      int x0, int y0, int z0, int x1, int y1, int z1,
                      std::vector<uint32_t>& changed) {
    const std::vector<uint8_t> before = cells;   // pre-step snapshot (O(grid); halo-only is a future optimization)
    auto at = [&](int x, int y, int z) -> uint8_t {
        if (x < 0 || x >= d.extent || y < 0 || y >= d.height_cells || z < 0 || z >= d.extent)
            return (uint8_t)VoxMat::Rock;          // OOB is inert
        return before[ca_cell_index(d, x, y, z)];
    };
    const int NX[6] = {1,-1,0,0,0,0}, NY[6] = {0,0,1,-1,0,0}, NZ[6] = {0,0,0,0,1,-1};
    for (int z = z0; z <= z1; ++z)
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            uint8_t m = at(x, y, z);
            int idx = ca_cell_index(d, x, y, z);
            bool nbFire = false, nbWater = false, hasAir = false;
            int ax = 0, ay = 0, az = 0;
            for (int k = 0; k < 6; ++k) {
                uint8_t nm = at(x + NX[k], y + NY[k], z + NZ[k]);
                if (nm == (uint8_t)VoxMat::Fire)  nbFire = true;
                if (nm == (uint8_t)VoxMat::Water) nbWater = true;
                if (!hasAir && nm == (uint8_t)VoxMat::Air) { hasAir = true; ax = x+NX[k]; ay = y+NY[k]; az = z+NZ[k]; }
            }
            if (m == (uint8_t)VoxMat::Fire) {
                if (nbWater) { cells[idx] = (uint8_t)VoxMat::Smoke; changed.push_back((uint32_t)idx); continue; }
                if (rnd01(x,y,z,step,seed,2) < p.burn_out_chance) {
                    cells[idx] = (uint8_t)VoxMat::Ash; changed.push_back((uint32_t)idx); continue;
                }
                if (hasAir && rnd01(x,y,z,step,seed,3) < p.smoke_chance) {
                    int aidx = ca_cell_index(d, ax, ay, az);
                    cells[aidx] = (uint8_t)VoxMat::Smoke; changed.push_back((uint32_t)aidx);   // idempotent
                }
                continue;   // fire stays
            }
            if (is_fuel(m) && nbFire) {
                float fl = material_props((VoxMat)m).flammability;
                if (rnd01(x,y,z,step,seed,1) < fl * p.ignite_scale) {
                    cells[idx] = (uint8_t)VoxMat::Fire; changed.push_back((uint32_t)idx);
                }
                continue;
            }
            if (m == (uint8_t)VoxMat::Smoke) {
                if (rnd01(x,y,z,step,seed,4) < p.smoke_dissipate_chance) {
                    cells[idx] = (uint8_t)VoxMat::Air; changed.push_back((uint32_t)idx);
                }
                continue;
            }
        }
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
