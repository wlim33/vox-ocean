#include <metal_stdlib>
#include "shader_types.h"
#include "voxel_grid.h"
using namespace metal;

// Apply a discrete-grid edit list: write the final material at each changed
// cell. The CPU diff already encodes reverts-to-terrain as an edit whose
// material is the terrain value, so this kernel needs no terrain lookup.
// One thread per edit. discrete_grid persists across frames (deltas accumulate).
kernel void apply_edits(
    constant ApplyEditsUniforms& U                 [[buffer(0)]],
    const device uint*  cells                       [[buffer(1)]],
    const device uchar* materials                   [[buffer(2)]],
    texture3d<uint, access::write> discrete_grid    [[texture(0)]],
    uint gid [[thread_position_in_grid]])
{
    if ((int)gid >= U.edit_count) return;
    VoxelGridDesc g = { U.grid_extent, U.height_cells, 0.0f, 0.0f, 0.0f };
    VgCell c = vg_decode_index(g, (int)cells[gid]);
    discrete_grid.write(uint4((uint)materials[gid], 0, 0, 0),
                        uint3((uint)c.ix, (uint)c.iy, (uint)c.iz));
}
