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

// Per-cell classification fed to resolve_block.
enum : uint8_t { CA_EMPTY = 0, CA_SAND = 1, CA_BARRIER = 2 };

// Resolve gravity within one 2x2x2 block, in place. Local index = lx + 2*ly + 4*lz
// (ly grows upward). Moves CA_SAND down into CA_EMPTY; CA_BARRIER is immovable.
// Deterministic; conserves the CA_SAND count.
void resolve_block(uint8_t cls[8]);

// One Margolus phase over the inclusive cell box [x0,x1]x[y0,y1]x[z0,z1] with
// block-origin offset (ox,oy,oz) in {0,1}. Routes cells by material phase from
// the registry (Granular→CA_SAND, Solid→CA_BARRIER, Empty→CA_EMPTY).
// Appends changed cell indices to `changed`.
void margolus_sweep(std::vector<uint8_t>& cells, const MaterialCaDims& d,
                    int ox, int oy, int oz,
                    int x0, int y0, int z0, int x1, int y1, int z1,
                    std::vector<uint32_t>& changed);

// Stateful stepper: phase schedule + dirty bounding box so settled regions cost
// nothing. One step = one Margolus sweep over the active box; the box follows the
// cells that changed (±1) and empties when nothing moves.
class MaterialCa {
public:
    void reset() { phase_ = 0; quiet_ = 0; clear_box(); }
    void wake_box(int x0, int y0, int z0, int x1, int y1, int z1);
    void step(std::vector<uint8_t>& cells, const MaterialCaDims& d,
              std::vector<uint32_t>& changed);
    bool awake() const { return ax1_ >= ax0_ && ay1_ >= ay0_ && az1_ >= az0_; }
private:
    void clear_box() { ax0_ = ay0_ = az0_ = 1; ax1_ = ay1_ = az1_ = 0; }  // empty (min>max)
    int phase_ = 0;
    int quiet_ = 0;     // consecutive no-motion phases; sleep only after a full cycle
    int ax0_ = 1, ay0_ = 1, az0_ = 1, ax1_ = 0, ay1_ = 0, az1_ = 0;
};
}
