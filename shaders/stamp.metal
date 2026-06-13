#include <metal_stdlib>
#include "shader_types.h"
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
    uint ix = i % (uint)U.grid_extent;
    uint iy = (i / (uint)U.grid_extent) % (uint)U.height_cells;
    uint iz = i / ((uint)U.grid_extent * (uint)U.height_cells);
    if ((int)iz >= U.grid_extent) return;
    world.write(uint4((uint)materials[gid], 0, 0, 0), uint3(ix, iy, iz));
}
