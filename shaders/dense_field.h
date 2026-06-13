#pragma once
#include "shader_types.h"
#include "voxel_grid.h"
#ifdef __METAL_VERSION__
// Dense representation: one material byte per cell from the R8 world grid.
inline uint vox_read(VoxelGridDesc g, texture3d<uint, access::read> world, int3 cell) {
    return world.read(uint3(cell)).r;
}
#endif
