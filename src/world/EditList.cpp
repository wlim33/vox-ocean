#include "world/EditList.h"
#include <cassert>
namespace vox {

void diff_cells(const std::vector<uint8_t>& prev, const std::vector<uint8_t>& cur, EditList& out) {
    assert(prev.size() == cur.size() && "diff_cells requires equal-length grids");
    out.clear();
    for (size_t i = 0; i < cur.size(); ++i)
        if (prev[i] != cur[i])
            out.push((uint32_t)i, cur[i]);
}

void apply_edits(std::vector<uint8_t>& grid, const EditList& edits) {
    for (int k = 0; k < edits.count(); ++k) {
        uint32_t i = edits.idx[k];
        if (i < grid.size())
            grid[i] = edits.mat[k];
    }
}
}
