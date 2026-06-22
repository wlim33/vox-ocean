#pragma once
#include <mdspan>
#include <cstdint>
#include <vector>
namespace vox {

// CPU ergonomics layer over the flat material buffer. layout_left over extents
// (extent, height_cells, extent) reproduces EXACTLY
//   ix + extent*(iy + height_cells*iz)
// == vg_cell_index (shaders/voxel_grid.h) == ca_cell_index (MaterialCa.h).
// This is a read/write VIEW only; it is NOT the GPU upload-layout authority —
// vg_cell_index/ca_cell_index remain the source of truth for that.
using GridExtents = std::dextents<int, 3>;
using GridSpan    = std::mdspan<const uint8_t, GridExtents, std::layout_left>;
using GridSpanMut = std::mdspan<uint8_t,       GridExtents, std::layout_left>;

inline GridSpan grid_view(const uint8_t* mats, int extent, int height_cells) {
    return GridSpan(mats, extent, height_cells, extent);
}
inline GridSpanMut grid_view(uint8_t* mats, int extent, int height_cells) {
    return GridSpanMut(mats, extent, height_cells, extent);
}
inline GridSpan grid_view(const std::vector<uint8_t>& c, int extent, int height_cells) {
    return grid_view(c.data(), extent, height_cells);
}
inline GridSpanMut grid_view(std::vector<uint8_t>& c, int extent, int height_cells) {
    return grid_view(c.data(), extent, height_cells);
}

}  // namespace vox
