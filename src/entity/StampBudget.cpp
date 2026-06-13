#include "entity/StampBudget.h"
#include "core/Config.h"
#include "entity/Boat.h"
#include <algorithm>
#include <cmath>
namespace vox {
int kelp_stalk_count(const Config& c) {
    if (!c.kelp.enabled) return 0;
    long e = c.voxel.grid_extent;
    return (int)std::round(c.kelp.density * (double)(e * e));
}
int kelp_cells_per_stalk(const Config& c) {
    return std::max(1, (int)std::ceil(c.kelp.max_height_m / c.voxel.height_step_m));
}
int boat_max_cells(const Config& c) {
    int vratio = std::max(1, (int)std::lround(c.voxel.voxel_size_m / c.voxel.height_step_m));
    return BOAT_LEN * BOAT_HGT * BOAT_BEAM * vratio;
}
int max_stamp_cells(const Config& c) {
    int kelp = kelp_stalk_count(c) * kelp_cells_per_stalk(c);
    int fish = c.fish.enabled ? c.fish.school_count * c.fish.per_school * FISH_CELLS : 0;
    return kelp + fish + boat_max_cells(c);
}
}
