#pragma once
#include <vector>
#include <cstdint>
namespace vox {

// A per-frame delta on the discrete voxel grid: for each entry, cell `idx[k]`
// becomes material `mat[k]`. `resync == true` means the whole grid changed
// (first frame / after a terrain rebuild) and idx/mat are empty — the consumer
// must re-seed wholesale from the authoritative grid rather than apply deltas.
struct EditList {
    std::vector<uint32_t> idx;
    std::vector<uint8_t>  mat;
    bool resync = false;
    void clear() { idx.clear(); mat.clear(); resync = false; }
    void push(uint32_t cell, uint8_t m) { idx.push_back(cell); mat.push_back(m); }
    int  count() const { return (int)idx.size(); }
};

// Emit (i, cur[i]) for every cell where prev[i] != cur[i]. prev and cur must be
// the same length. Does not set `out.resync`. Clears `out` first.
void diff(const std::vector<uint8_t>& prev, const std::vector<uint8_t>& cur, EditList& out);

// grid[idx[k]] = mat[k] for each edit (the inverse of diff). `resync` is ignored
// here — the caller handles wholesale re-seeding.
void apply(std::vector<uint8_t>& grid, const EditList& edits);
}
