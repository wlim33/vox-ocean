#pragma once
#include <cstdint>
#include <vector>
namespace vox {

// Grid dims. extent spans x and z; height_cells spans y (gravity → -y).
struct MaterialCaDims { int extent; int height_cells; };

// x-fastest linear index — lockstep with VoxelWorld::cell_index and the GPU
// 3D-texture upload. (Cross-checked in material_ca_test.cpp.)
inline int ca_cell_index(const MaterialCaDims& d, int ix, int iy, int iz) {
    return ix + d.extent * (iy + d.height_cells * iz);
}

// Resolve gravity and fluid leveling within one 2x2x2 block, in place.
// `mat[8]` holds material ids; local index = lx + 2*ly + 4*lz (ly=0 is lower).
// Motion is driven by density/fluidity/movable from MaterialRegistry — no Phase
// switch. The operation is a permutation: per-material counts are conserved.
// Deterministic with fixed iteration order and single-target moves per cell.
void resolve_block(uint8_t mat[8]);

// One Margolus phase over the inclusive cell box [x0,x1]x[y0,y1]x[z0,z1] with
// block-origin offset (ox,oy,oz) in {0,1}. Reads and writes material ids
// directly; out-of-grid cells are treated as immovable Rock.
// Appends changed cell indices to `changed`.
void margolus_sweep(std::vector<uint8_t>& cells, const MaterialCaDims& d,
                    int ox, int oy, int oz,
                    int x0, int y0, int z0, int x1, int y1, int z1,
                    std::vector<uint32_t>& changed);

// Combustion dynamics rates (per-step probabilities), passed into the sweep.
struct CombustionParams {
    float burn_out_chance = 0.08f;        // P(Fire -> Ash) per step
    float smoke_chance = 0.30f;           // P(Fire emits Smoke into adjacent air) per step
    float smoke_dissipate_chance = 0.04f; // P(Smoke -> Air) per step
    float ignite_scale = 1.0f;            // multiplier on flammability for ignition prob
    float boil_chance = 0.5f;             // P(Water -> Steam | Fire neighbor) per step
    float condense_chance = 0.10f;        // P(Steam -> Water | no Fire neighbor) per step
    float cool_chance = 0.15f;            // P(Lava -> Rock | Water neighbor) per step
};

// Data-driven contact-reaction pass over the inclusive box: a Noita-style table of
// (A, B) -> (C, D) @rate rules where A is the cell and B a face-neighbour matched by
// material id, tag, "any", "hot", or "no-hot". Reads a pre-step snapshot of `cells`
// (order-independent) and writes the products. Randomness is hash(x,y,z,step,seed)
// -> reproducible. Out-of-grid neighbours read as inert Rock. Appends changed indices
// to `changed`. (Generalizes the former combustion_sweep; behaviour-preserving — see
// the equivalence oracle in material_ca_test.cpp.)
void contact_sweep(std::vector<uint8_t>& cells, const MaterialCaDims& d,
                   uint32_t step, uint32_t seed, const CombustionParams& p,
                   int x0, int y0, int z0, int x1, int y1, int z1,
                   std::vector<uint32_t>& changed);

// Per-step heat-field tunables. diffuse_k MUST stay <= 1/6 (6-neighbour explicit
// diffusion stability bound) or temperatures can oscillate/diverge.
struct ThermalParams {
    float   diffuse_k     = 0.16f;   // <= 1/6
    float   ambient_bleed = 0.5f;    // heat units/step pulled toward ambient
    uint8_t snap_eps      = 1;       // |T-ambient| <= eps -> snap to ambient (lets blocks sleep)
    uint8_t ignite_temp   = 150;     // Flammable-tagged -> Fire above this
    uint8_t boil_temp     = 120;     // Water -> Steam above
    uint8_t condense_temp = 80;      // Steam -> Water below
    uint8_t melt_temp     = 40;      // Ice -> Water above (Ice added in P5)
};

// Conductivity-weighted heat diffusion over the inclusive box, then heat-source
// re-assertion (emit_temp) and an ambient bleed. Reads a pre-step snapshot of
// `temp` so the result is order-independent. OOB neighbours read as ambient with
// Air conductivity. Writes `temp` in place; appends any material transitions to
// `changed` (none until thermal rules land).
void thermal_sweep(std::vector<uint8_t>& cells, std::vector<uint8_t>& temp,
                   const MaterialCaDims& d, const ThermalParams& tp, uint8_t ambient,
                   int x0, int y0, int z0, int x1, int y1, int z1,
                   std::vector<uint32_t>& changed);

// Stateful stepper: phase schedule + dirty bounding box so settled regions cost
// nothing. One step = one Margolus sweep over the active box; the box follows the
// cells that changed (±1) and empties when nothing moves.
class MaterialCa {
public:
    void reset() { phase_ = 0; quiet_ = 0; clear_box(); combustion_ = false; thermal_ = false; }
    void wake_box(int x0, int y0, int z0, int x1, int y1, int z1);
    // Non-thermal step (no heat field). Requires thermal disabled.
    void step(std::vector<uint8_t>& cells, const MaterialCaDims& d,
              std::vector<uint32_t>& changed);
    // Thermal step: diffuses `temp` and lets non-ambient heat keep the box awake.
    void step(std::vector<uint8_t>& cells, std::vector<uint8_t>& temp,
              const MaterialCaDims& d, std::vector<uint32_t>& changed);
    bool awake() const { return ax1_ >= ax0_ && ay1_ >= ay0_ && az1_ >= az0_; }
    void enable_combustion(uint32_t seed, CombustionParams p) {
        combustion_ = true; seed_ = seed; cparams_ = p;
    }
    void enable_thermal(ThermalParams tp, uint8_t ambient) {
        thermal_ = true; tparams_ = tp; ambient_ = ambient;
    }
private:
    void clear_box() { ax0_ = ay0_ = az0_ = 1; ax1_ = ay1_ = az1_ = 0; }  // empty (min>max)
    int phase_ = 0;
    int quiet_ = 0;     // consecutive no-motion phases; sleep only after a full cycle
    int ax0_ = 1, ay0_ = 1, az0_ = 1, ax1_ = 0, ay1_ = 0, az1_ = 0;
    bool combustion_ = false;
    uint32_t seed_ = 0;
    CombustionParams cparams_{};
    bool thermal_ = false;
    uint8_t ambient_ = 0;
    ThermalParams tparams_{};
};
}
