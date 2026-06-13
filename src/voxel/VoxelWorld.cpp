#include "voxel/VoxelWorld.h"
#include "voxel_grid.h"
namespace vox {
float VoxelWorld::column_center_x(int ix) const { return vg_column_center(desc_(), ix); }
float VoxelWorld::quantize_height(float h) const { return vg_quantize_height(desc_(), h); }
int   VoxelWorld::water_top_cell(float h)  const { return vg_water_top_cell(desc_(), h); }
}
