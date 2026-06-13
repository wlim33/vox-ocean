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

// Per-fish silhouette: a small body, FISH_BODY_LEN along the heading by
// FISH_BODY_HGT tall, so each fish has real voxel cross-section through water.
inline constexpr int FISH_BODY_LEN = 3;
inline constexpr int FISH_BODY_HGT = 3;
inline constexpr int FISH_CELLS    = FISH_BODY_LEN * FISH_BODY_HGT;

// Exact stalk count: density directly sets the number, so the stamp capacity
// is an exact (not worst-case) bound. Mirrored by KelpBed::rebuild.
int kelp_stalk_count(const Config&);
int kelp_cells_per_stalk(const Config&);   // ceil(max_height_m / height_step_m)

int boat_max_cells(const Config&);   // BOAT_LEN*BOAT_HGT*BOAT_BEAM * vratio — exact upper bound

// Upper bound on cells stamped in one frame: kelp bed + all fish + boat.
int max_stamp_cells(const Config&);
}
