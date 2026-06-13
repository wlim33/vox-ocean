#pragma once
#include "voxel/VoxelWorld.h"   // VoxMat
#include <cstdint>
#include <vector>
namespace vox {
struct Config;

// Per-cell stamp list: entities append (cell index, material) pairs; the
// renderer uploads idx/mat in parallel and one dispatch writes them. Append
// order IS priority — the last writer to a cell wins (kelp, then fish, then
// the boat on top).
struct StampList {
    std::vector<uint32_t> idx;
    std::vector<uint8_t>  mat;
    void clear() { idx.clear(); mat.clear(); }
    void push(uint32_t cell, VoxMat m) { idx.push_back(cell); mat.push_back((uint8_t)m); }
    int  count() const { return (int)idx.size(); }
};

// Per-fish silhouette voxels, and the boat's worst-case cell count (its hull
// AABB; matches M4's old MAX_STAMP_CELLS bound).
inline constexpr int FISH_CELLS     = 5;
inline constexpr int BOAT_MAX_CELLS = 256;

// Exact stalk count: density directly sets the number, so the stamp capacity
// is an exact (not worst-case) bound. Mirrored by KelpBed::rebuild.
int kelp_stalk_count(const Config&);
int kelp_cells_per_stalk(const Config&);   // ceil(max_height_m / height_step_m)

// Upper bound on cells stamped in one frame: kelp bed + all fish + boat.
int max_stamp_cells(const Config&);
}
