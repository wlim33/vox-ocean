#include <metal_stdlib>
#include "shader_types.h"
#include "voxel_grid.h"
using namespace metal;

// Writes per-cell materials into a CPU-supplied list of packed cell indices,
// after world_fill so entities overwrite water. Index decode is the lockstep
// mirror of VoxelWorld::decode_cell_index.
kernel void stamp_cells(
    constant StampUniforms& U              [[buffer(0)]],
    const device uint*  cells              [[buffer(1)]],
    const device uchar* materials          [[buffer(2)]],
    texture3d<uint, access::write> world   [[texture(0)]],
    uint gid [[thread_position_in_grid]])
{
    if ((int)gid >= U.count) return;
    uint i  = cells[gid];
    VoxelGridDesc g = {U.grid_extent, U.height_cells, 0.0f, 0.0f, 0.0f};  // decode uses int fields only
    VgCell c = vg_decode_index(g, (int)i);
    if (c.iz >= U.grid_extent) return;
    world.write(uint4((uint)materials[gid], 0, 0, 0), uint3((uint)c.ix, (uint)c.iy, (uint)c.iz));
}
