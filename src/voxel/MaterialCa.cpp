#include "voxel/MaterialCa.h"
#include "voxel/VoxelWorld.h"   // VoxMat
#include "voxel/MaterialRegistry.h"
#include <algorithm>
#include <cmath>
namespace vox {

namespace {
inline float ca_density(uint8_t m) { return kDensity[m]; }
inline bool  ca_movable(uint8_t m) { return kMovable[m]; }
// fluidity >= 0.5 -> levels laterally (liquid); < 0.5 -> repose (granular).
inline bool  ca_levels (uint8_t m) { return kFluidity[m] >= 0.5f; }
// `a` may displace `b` only if both are movable and a is strictly denser than b.
inline bool  can_sink  (uint8_t a, uint8_t b) {
    return ca_movable(a) && ca_movable(b) && ca_density(a) > ca_density(b);
}
// True if any of the 6 face-neighbours of (x,y,z) satisfies `pred`. OOB cells are
// skipped: the out-of-grid wall is a read sentinel for the sweeps, never a
// reaction participant, so it must not keep the CA awake either.
template <class Pred>
inline bool any_face_neighbour(const std::vector<uint8_t>& cells, const MaterialCaDims& d,
                               int x, int y, int z, Pred pred) {
    static const int NX[6]={1,-1,0,0,0,0}, NY[6]={0,0,1,-1,0,0}, NZ[6]={0,0,0,0,1,-1};
    for (int k = 0; k < 6; ++k) {
        int nx=x+NX[k], ny=y+NY[k], nz=z+NZ[k];
        if (nx<0||nx>=d.extent||ny<0||ny>=d.height_cells||nz<0||nz>=d.extent) continue;
        if (pred(cells[ca_cell_index(d, nx, ny, nz)])) return true;
    }
    return false;
}
// True if a cell holds a material that must keep the CA awake under contact
// reactions: Fire/Smoke/Steam unconditionally; Lava only while touching Water,
// Acid only while touching a [Corrodible] neighbour (settled lava-in-air and
// inert acid are allowed to sleep). Mirrors the reactive rows of kContacts and
// is the single source of the predicate the two step() scans share.
inline bool is_reactive_cell(const std::vector<uint8_t>& cells, const MaterialCaDims& d,
                             int x, int y, int z) {
    uint8_t m = cells[ca_cell_index(d, x, y, z)];
    if (m == (uint8_t)VoxMat::Fire || m == (uint8_t)VoxMat::Smoke || m == (uint8_t)VoxMat::Steam)
        return true;
    if (m == (uint8_t)VoxMat::Lava)
        return any_face_neighbour(cells, d, x, y, z,
                                  [](uint8_t n) { return n == (uint8_t)VoxMat::Water; });
    if (m == (uint8_t)VoxMat::Acid)
        return any_face_neighbour(cells, d, x, y, z,
                                  [](uint8_t n) { return material_has_tag((VoxMat)n, MatTag::Corrodible); });
    return false;
}
// A cell whose temperature is off ambient must keep the box awake so heat keeps
// diffusing (and eventually snaps back to ambient, letting the box sleep).
inline bool temp_active(const std::vector<uint8_t>& temp, const MaterialCaDims& d,
                        int x, int y, int z, uint8_t ambient) {
    return temp[ca_cell_index(d, x, y, z)] != ambient;
}

// Pre-step snapshot of just the active box + a 1-cell halo. The contact/thermal
// kernels reach at most ±1 from each box cell, so this compact sub-volume holds
// every pre-step value they read — replacing the old full-grid copy (2.25 MiB)
// with O(box) bytes. Truly out-of-grid reads are still the caller's job (sentinel);
// any in-grid read the kernel makes is guaranteed to land inside the halo.
struct HaloSnapshot {
    int x0_, y0_, z0_;          // halo origin in grid coords (clamped to >= 0)
    int sx_, sy_;               // halo x/y extents (z stride = sx_*sy_)
    std::vector<uint8_t> buf_;
    HaloSnapshot(const std::vector<uint8_t>& src, const MaterialCaDims& d,
                 int bx0, int by0, int bz0, int bx1, int by1, int bz1) {
        x0_ = std::max(0, bx0 - 1); y0_ = std::max(0, by0 - 1); z0_ = std::max(0, bz0 - 1);
        int x1 = std::min(d.extent - 1, bx1 + 1);
        int y1 = std::min(d.height_cells - 1, by1 + 1);
        int z1 = std::min(d.extent - 1, bz1 + 1);
        sx_ = x1 - x0_ + 1; sy_ = y1 - y0_ + 1;
        int sz = z1 - z0_ + 1;
        buf_.resize((size_t)sx_ * sy_ * sz);
        // x is contiguous in both grid and halo, so copy one row per (y,z).
        for (int z = z0_; z <= z1; ++z)
            for (int y = y0_; y <= y1; ++y) {
                const uint8_t* s = &src[ca_cell_index(d, x0_, y, z)];
                std::copy(s, s + sx_, &buf_[local(x0_, y, z)]);
            }
    }
    size_t local(int x, int y, int z) const {
        return (size_t)(x - x0_) + (size_t)sx_ * ((y - y0_) + (size_t)sy_ * (z - z0_));
    }
    uint8_t at(int x, int y, int z) const { return buf_[local(x, y, z)]; }  // in-grid only
};
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
    // Non-thermal callers forward to the thermal body with an unused buffer; thermal_
    // is false here so `temp` is never read.
    std::vector<uint8_t> unused;
    step(cells, unused, d, changed);
}

void MaterialCa::step(std::vector<uint8_t>& cells, std::vector<uint8_t>& temp,
                      const MaterialCaDims& d, std::vector<uint32_t>& changed) {
    if (!awake()) return;
    if (thermal_)
        thermal_sweep(cells, temp, d, tparams_, ambient_,
                      ax0_, ay0_, az0_, ax1_, ay1_, az1_, changed);
    if (combustion_)
        contact_sweep(cells, d, (uint32_t)phase_, seed_, cparams_,
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
    // Heat in flight keeps the box awake too: a cell off ambient must keep diffusing.
    bool reactive_present = false;
    if ((combustion_ || thermal_) && changed.empty()) {
        for (int iz = az0_; iz <= az1_ && !reactive_present; ++iz)
            for (int iy = ay0_; iy <= ay1_ && !reactive_present; ++iy)
                for (int ix = ax0_; ix <= ax1_ && !reactive_present; ++ix)
                    if ((combustion_ && is_reactive_cell(cells, d, ix, iy, iz)) ||
                        (thermal_ && temp_active(temp, d, ix, iy, iz, ambient_)))
                        reactive_present = true;
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
    if (combustion_ || thermal_) {
        for (int iz = z0; iz <= z1; ++iz)
            for (int iy = y0; iy <= y1; ++iy)
                for (int ix = x0; ix <= x1; ++ix)
                    if ((combustion_ && is_reactive_cell(cells, d, ix, iy, iz)) ||
                        (thermal_ && temp_active(temp, d, ix, iy, iz, ambient_))) {
                        nx0 = std::min(nx0, ix - 1); nx1 = std::max(nx1, ix + 1);
                        ny0 = std::min(ny0, iy - 1); ny1 = std::max(ny1, iy + 1);
                        nz0 = std::min(nz0, iz - 1); nz1 = std::max(nz1, iz + 1);
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
inline bool is_hot(uint8_t m) { return m == (uint8_t)VoxMat::Fire || m == (uint8_t)VoxMat::Lava; }

// --- Thermal threshold transitions -------------------------------------------
// A cell transforms when its own temperature crosses `threshold`. The matcher is a
// specific material OR a tag (so any Flammable ignites). Heat-source materials
// (Lava, Fire) are intentionally absent: they self-heat via emit_temp, so they
// expire/solidify through contact rules, never here. TMatch/TCmp are reused by the
// contact-reaction table below.
enum class TMatch : uint8_t { Material, Tag };
enum class TCmp   : uint8_t { Above, Below };
struct ThermalRule {
    TMatch  match; uint32_t key;            // VoxMat id, or MatTag bits
    TCmp    cmp;   uint8_t ThermalParams::* threshold;
    VoxMat  output;
};
constexpr ThermalRule kThermalRules[] = {
    { TMatch::Tag,      (uint32_t)MatTag::Flammable, TCmp::Above, &ThermalParams::ignite_temp,   VoxMat::Fire  },
    { TMatch::Material, (uint32_t)VoxMat::Water,     TCmp::Above, &ThermalParams::boil_temp,     VoxMat::Steam },
    { TMatch::Material, (uint32_t)VoxMat::Steam,     TCmp::Below, &ThermalParams::condense_temp, VoxMat::Water },
    { TMatch::Material, (uint32_t)VoxMat::Ice,       TCmp::Above, &ThermalParams::melt_temp,     VoxMat::Water },
};
inline bool tmatch(const ThermalRule& r, uint8_t m) {
    return r.match == TMatch::Material ? (m == (uint8_t)r.key)
                                       : material_has_tag((VoxMat)m, (MatTag)r.key);
}

// --- Data-driven contact-reaction table --------------------------------------
// Each row is a Noita-style pair rule: reagent A is the cell, reagent B a face-
// neighbour. A is matched by material id or tag (TMatch, shared with the thermal
// table). B is matched by BMatch: a specific material, a tag, "Any" neighbour,
// "Hot" (Fire/Lava), or "NoHot" (gate: fires only when NO neighbour is hot, with no
// B target). `rate` is a member-pointer into CombustionParams (nullptr = always);
// `scale_flam` multiplies the chance by the cell's flammability (ignite). Products:
// a_out replaces the cell unless keep_a; b_out replaces the matched neighbour unless
// keep_b. The per-row `salt` reproduces the original draws so this generalization is
// behaviour-preserving (verified by the equivalence oracle in the tests).
enum class BMatch : uint8_t { Material, Tag, Any, Hot, NoHot };

struct ContactRule {
    TMatch   a_match; uint32_t a_key;          // cell reagent (material id | tag bits)
    BMatch   b_match; uint32_t b_key;          // neighbour reagent (key for Material/Tag)
    float CombustionParams::* rate;            // nullptr = deterministic (no RNG draw)
    bool     scale_flam;                       // chance *= cell flammability (ignite)
    VoxMat   a_out;   bool keep_a;
    VoxMat   b_out;   bool keep_b;
    uint32_t salt;                             // unused when rate==nullptr
};

constexpr ContactRule kContacts[] = {
    // Fire cluster — table order = priority (water-extinguish > burn-out > smoke-emit).
    { TMatch::Material,(uint32_t)VoxMat::Fire,  BMatch::Material,(uint32_t)VoxMat::Water,
      nullptr, false, VoxMat::Smoke,false, VoxMat::Air,true, 0 },
    { TMatch::Material,(uint32_t)VoxMat::Fire,  BMatch::Any,0,
      &CombustionParams::burn_out_chance, false, VoxMat::Ash,false, VoxMat::Air,true, 2 },
    { TMatch::Material,(uint32_t)VoxMat::Fire,  BMatch::Material,(uint32_t)VoxMat::Air,
      &CombustionParams::smoke_chance, false, VoxMat::Fire,true, VoxMat::Smoke,false, 3 },
    // Ignition: any Flammable cell next to a heat source.
    { TMatch::Tag,(uint32_t)MatTag::Flammable,  BMatch::Hot,0,
      &CombustionParams::ignite_scale, true, VoxMat::Fire,false, VoxMat::Air,true, 1 },
    // Smoke dissipates.
    { TMatch::Material,(uint32_t)VoxMat::Smoke, BMatch::Any,0,
      &CombustionParams::smoke_dissipate_chance, false, VoxMat::Air,false, VoxMat::Air,true, 4 },
    // Water boils next to a heat source.
    { TMatch::Material,(uint32_t)VoxMat::Water, BMatch::Hot,0,
      &CombustionParams::boil_chance, false, VoxMat::Steam,false, VoxMat::Air,true, 5 },
    // Steam condenses when no heat source is adjacent (NoHot gate, self-only).
    { TMatch::Material,(uint32_t)VoxMat::Steam, BMatch::NoHot,0,
      &CombustionParams::condense_chance, false, VoxMat::Water,false, VoxMat::Air,true, 6 },
    // Lava crusts to rock against water (water kept — thermodynamic steam handled
    // by the thermal layer / future enhancement).
    { TMatch::Material,(uint32_t)VoxMat::Lava,  BMatch::Material,(uint32_t)VoxMat::Water,
      &CombustionParams::cool_chance, false, VoxMat::Rock,false, VoxMat::Air,true, 7 },
    // Acid dissolves any [Corrodible] neighbour into flammable gas (acid persists).
    { TMatch::Material,(uint32_t)VoxMat::Acid,  BMatch::Tag,(uint32_t)MatTag::Corrodible,
      &CombustionParams::acid_chance, false, VoxMat::Acid,true, VoxMat::FlammableGas,false, 8 },
};

inline bool a_matches(TMatch mt, uint32_t key, uint8_t m) {
    return mt == TMatch::Material ? (m == (uint8_t)key)
                                  : material_has_tag((VoxMat)m, (MatTag)key);
}
inline bool b_matches(BMatch bm, uint32_t key, uint8_t m) {
    switch (bm) {
        case BMatch::Any:      return true;
        case BMatch::Hot:      return is_hot(m);
        case BMatch::NoHot:    return !is_hot(m);
        case BMatch::Material: return m == (uint8_t)key;
        case BMatch::Tag:      return material_has_tag((VoxMat)m, (MatTag)key);
    }
    return false;
}

// Compile-time guard: valid outputs, and every Fire row precedes the first non-Fire
// row so the priority cluster cannot be reordered by a later edit.
constexpr bool contacts_well_formed() {
    bool seen_non_fire = false;
    for (const ContactRule& r : kContacts) {
        if ((size_t)r.a_out >= (size_t)kNumMaterials) return false;
        if ((size_t)r.b_out >= (size_t)kNumMaterials) return false;
        bool is_fire_row = (r.a_match == TMatch::Material && r.a_key == (uint32_t)VoxMat::Fire);
        if (!is_fire_row)       seen_non_fire = true;
        else if (seen_non_fire) return false;
    }
    return true;
}
static_assert(contacts_well_formed(),
              "kContacts: invalid output id or Fire rows out of priority order");
}

void thermal_sweep(std::vector<uint8_t>& cells, std::vector<uint8_t>& temp,
                   const MaterialCaDims& d, const ThermalParams& tp, uint8_t ambient,
                   int x0, int y0, int z0, int x1, int y1, int z1,
                   std::vector<uint32_t>& changed) {
    const HaloSnapshot before(temp, d, x0, y0, z0, x1, y1, z1);   // box+halo (order-independence)
    const HaloSnapshot mats(cells, d, x0, y0, z0, x1, y1, z1);    // pre-step materials: a threshold
    // transition earlier in the sweep must not change the conductivity later cells see.
    const float kAirCond = kConductivity[(uint8_t)VoxMat::Air];
    auto T = [&](int x, int y, int z) -> float {  // OOB temperature reads as ambient
        if (x < 0 || x >= d.extent || y < 0 || y >= d.height_cells || z < 0 || z >= d.extent)
            return (float)ambient;
        return (float)before.at(x, y, z);
    };
    auto C = [&](int x, int y, int z) -> float {  // OOB conductivity reads as Air
        if (x < 0 || x >= d.extent || y < 0 || y >= d.height_cells || z < 0 || z >= d.extent)
            return kAirCond;
        return kConductivity[mats.at(x, y, z)];
    };
    const int NX[6] = {1,-1,0,0,0,0}, NY[6] = {0,0,1,-1,0,0}, NZ[6] = {0,0,0,0,1,-1};
    for (int z = z0; z <= z1; ++z)
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            int i = ca_cell_index(d, x, y, z);
            // --- diffusion kernel: conductivity-weighted Laplacian ----------------
            // Heat flows from hot to cold across each face, throttled by the SLOWER
            // of the two media (min conductivity) so an insulator on either side
            // blocks the flux. diffuse_k <= 1/6 keeps the explicit update stable.
            float t = T(x, y, z), cs = C(x, y, z), flux = 0.0f;
            for (int k = 0; k < 6; ++k) {
                float w = std::min(cs, C(x + NX[k], y + NY[k], z + NZ[k]));
                flux += w * (T(x + NX[k], y + NY[k], z + NZ[k]) - t);
            }
            float nt = t + tp.diffuse_k * flux;
            // ----------------------------------------------------------------------
            // Heat source: re-assert own temperature up to emit_temp every step.
            int emit = kEmitTemp[cells[i]];
            if (emit >= 0) {
                if (nt < (float)emit) nt = (float)emit;
            } else {
                // Ambient bleed + snap so settled cells reach exactly ambient and sleep.
                if (nt > ambient)      nt = std::max((float)ambient, nt - tp.ambient_bleed);
                else if (nt < ambient) nt = std::min((float)ambient, nt + tp.ambient_bleed);
                if (std::abs(nt - (float)ambient) <= (float)tp.snap_eps) nt = (float)ambient;
            }
            temp[i] = (uint8_t)std::clamp((int)std::lround(nt), 0, 255);
            // Threshold transition against the just-written temperature.
            for (const ThermalRule& r : kThermalRules) {
                if (!tmatch(r, cells[i])) continue;
                uint8_t thr = tp.*r.threshold;
                bool hit = (r.cmp == TCmp::Above) ? temp[i] > thr : temp[i] < thr;
                if (!hit) continue;
                cells[i] = (uint8_t)r.output;
                changed.push_back((uint32_t)i);
                break;
            }
        }
}

void contact_sweep(std::vector<uint8_t>& cells, const MaterialCaDims& d,
                   uint32_t step, uint32_t seed, const CombustionParams& p,
                   int x0, int y0, int z0, int x1, int y1, int z1,
                   std::vector<uint32_t>& changed) {
    const HaloSnapshot before(cells, d, x0, y0, z0, x1, y1, z1);  // box+halo (was full-grid copy)
    auto at = [&](int x, int y, int z) -> uint8_t {
        if (x < 0 || x >= d.extent || y < 0 || y >= d.height_cells || z < 0 || z >= d.extent)
            return (uint8_t)VoxMat::Rock;          // OOB is inert
        return before.at(x, y, z);
    };
    const int NX[6] = {1,-1,0,0,0,0}, NY[6] = {0,0,1,-1,0,0}, NZ[6] = {0,0,0,0,1,-1};
    for (int z = z0; z <= z1; ++z)
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            uint8_t m = at(x, y, z);
            int idx = ca_cell_index(d, x, y, z);
            for (const ContactRule& r : kContacts) {
                if (!a_matches(r.a_match, r.a_key, m)) continue;
                // Find the first face-neighbour satisfying B (in fixed NX/NY/NZ order).
                // NoHot is a gate over all neighbours, not a per-neighbour target.
                int bi = -1;
                if (r.b_match == BMatch::NoHot) {
                    bool any_hot = false;
                    for (int k = 0; k < 6 && !any_hot; ++k)
                        if (is_hot(at(x+NX[k], y+NY[k], z+NZ[k]))) any_hot = true;
                    if (any_hot) continue;
                } else {
                    // The OOB halo is an inert wall: a valid READ sentinel for gates,
                    // but never a writable B target (Rock is Corrodible, so Acid would
                    // otherwise "dissolve" the wall and write outside the grid).
                    for (int k = 0; k < 6; ++k) {
                        int nx = x+NX[k], ny = y+NY[k], nz = z+NZ[k];
                        if (nx < 0 || nx >= d.extent || ny < 0 || ny >= d.height_cells ||
                            nz < 0 || nz >= d.extent) continue;
                        if (b_matches(r.b_match, r.b_key, at(nx, ny, nz))) {
                            bi = ca_cell_index(d, nx, ny, nz); break;
                        }
                    }
                    if (bi < 0) continue;                       // no matching neighbour
                }
                if (r.rate) {                                   // nullptr = deterministic
                    float chance = p.*r.rate;
                    if (r.scale_flam) chance *= kFlammability[m];
                    if (rnd01(x, y, z, step, seed, r.salt) >= chance) continue;   // roll fails -> next row
                }
                if (!r.keep_a) { cells[idx] = (uint8_t)r.a_out; changed.push_back((uint32_t)idx); }
                if (!r.keep_b && bi >= 0) { cells[bi] = (uint8_t)r.b_out; changed.push_back((uint32_t)bi); }
                break;                                          // first success wins
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
