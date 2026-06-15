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

// Revert last frame's entity cells to base material (terrain, else water/air at the
// column's current water level from prev_water). Run before this frame's stamp.
kernel void destamp_cells(
    constant StampUniforms& U              [[buffer(0)]],
    const device uint*  cells              [[buffer(1)]],
    const device int*   prev_water         [[buffer(2)]],
    texture3d<uint, access::read>  terrain [[texture(0)]],
    texture3d<uint, access::write> world   [[texture(1)]],
    uint gid [[thread_position_in_grid]])
{
    if ((int)gid >= U.count) return;
    uint i = cells[gid];
    VoxelGridDesc g = {U.grid_extent, U.height_cells, 0.0f, 0.0f, 0.0f};
    VgCell c = vg_decode_index(g, (int)i);
    if (c.iz >= U.grid_extent) return;
    uint t = terrain.read(uint3((uint)c.ix, (uint)c.iy, (uint)c.iz)).r;
    int wtop = prev_water[c.iz * U.grid_extent + c.ix]; // prev_water already holds THIS frame's water_top (world_fill_incremental ran first)
    uint m = (t != MAT_AIR) ? t : ((c.iy < wtop) ? MAT_WATER : MAT_AIR);
    world.write(uint4(m, 0, 0, 0), uint3((uint)c.ix, (uint)c.iy, (uint)c.iz));
}
