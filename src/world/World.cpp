#include "world/World.h"
#include "core/Config.h"
#include <algorithm>
#include <cmath>
namespace vox {

void World::configure(const Config& cfg) {
    const VoxelConfig& v = cfg.voxel;
    bool unchanged = grid_
        && v.grid_extent   == built_extent_      && v.height_cells == built_height_cells_
        && v.floor_seed    == built_seed_        && v.base_depth_m  == built_base_depth_
        && v.height_step_m == built_height_step_ && v.voxel_size_m  == built_voxel_size_;
    if (unchanged) return;

    grid_.emplace(VoxelWorldParams{ v.grid_extent, v.height_cells, v.voxel_size_m,
                                    v.height_step_m, v.base_depth_m });
    floor_ = generate_floor({ v.grid_extent, v.height_cells, (uint32_t)v.floor_seed,
                              v.base_depth_m, v.height_step_m });

    size_t cells = (size_t)grid_->cells();
    terrain_.assign(cells, (uint8_t)VoxMat::Air);
    int extent = v.grid_extent, hc = v.height_cells;
    for (int iz = 0; iz < extent; ++iz)
        for (int ix = 0; ix < extent; ++ix) {
            const FloorColumn& fc = floor_[(size_t)iz * extent + ix];
            for (int iy = 0; iy < fc.height && iy < hc; ++iy)
                terrain_[grid_->cell_index(ix, iy, iz)] = fc.material;
        }
    cells_ = terrain_;   // initial overlay = bare terrain

    built_extent_     = v.grid_extent;   built_height_cells_ = v.height_cells;
    built_seed_       = v.floor_seed;    built_base_depth_   = v.base_depth_m;
    built_height_step_= v.height_step_m; built_voxel_size_   = v.voxel_size_m;
}

void World::begin_frame() {
    cells_ = terrain_;
}

void World::ingest(const StampList& stamps) {
    for (int i = 0; i < stamps.count(); ++i)
        cells_[stamps.idx[i]] = stamps.mat[i];   // last writer wins
}

float World::floor_top_y(float x, float z) const {
    const VoxelWorldParams& p = grid_->params();
    if (floor_.empty()) return -p.base_depth_m;
    float half = 0.5f * p.extent * p.voxel_size_m;
    int ix = std::clamp((int)std::floor((x + half) / p.voxel_size_m), 0, p.extent - 1);
    int iz = std::clamp((int)std::floor((z + half) / p.voxel_size_m), 0, p.extent - 1);
    int h = floor_[(size_t)iz * p.extent + ix].height;
    return -p.base_depth_m + (float)h * p.height_step_m;
}
}
