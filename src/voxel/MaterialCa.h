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
// block-origin offset (ox,oy,oz) in {0,1}. terrain_top[iz*extent+ix] = first cell
// index-in-column at/above which Sand is dynamic; Sand below it is terrain (barrier).
// Appends changed cell indices to `changed`.
void margolus_sweep(std::vector<uint8_t>& cells, const MaterialCaDims& d,
                    const std::vector<uint8_t>& terrain_top,
                    int ox, int oy, int oz,
                    int x0, int y0, int z0, int x1, int y1, int z1,
                    std::vector<uint32_t>& changed);
}
