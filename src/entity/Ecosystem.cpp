#include "entity/Ecosystem.h"
#include "core/Config.h"
#include <algorithm>
#include <cmath>
namespace vox {

void Ecosystem::rebuild_if_dirty(const Config& cfg) {
    bool floor_dirty = cfg.voxel.grid_extent != built_extent_
                    || cfg.voxel.height_cells != built_height_cells_
                    || cfg.voxel.floor_seed   != built_floor_seed_;
    if (floor_dirty) {
        floor_ = generate_floor({ cfg.voxel.grid_extent, cfg.voxel.height_cells,
                                  (uint32_t)cfg.voxel.floor_seed,
                                  cfg.voxel.base_depth_m, cfg.voxel.height_step_m });
        built_extent_       = cfg.voxel.grid_extent;
        built_height_cells_ = cfg.voxel.height_cells;
        built_floor_seed_   = cfg.voxel.floor_seed;
    }
    if (floor_dirty
        || cfg.kelp.enabled      != built_kelp_enabled_
        || cfg.kelp.density      != built_kelp_density_
        || cfg.kelp.max_height_m != built_kelp_height_
        || cfg.kelp.seed         != built_kelp_seed_
        || cfg.voxel.height_step_m != built_kelp_step_) {
        kelp_.rebuild(cfg, floor_);
        built_kelp_enabled_ = cfg.kelp.enabled;
        built_kelp_density_ = cfg.kelp.density;
        built_kelp_height_  = cfg.kelp.max_height_m;
        built_kelp_seed_    = cfg.kelp.seed;
        built_kelp_step_    = cfg.voxel.height_step_m;
    }
    if (cfg.fish.enabled         != built_fish_enabled_
        || cfg.fish.school_count != built_school_count_
        || cfg.fish.per_school   != built_per_school_
        || cfg.fish.seed         != built_fish_seed_
        || cfg.fish.spread_m     != built_fish_spread_) {
        fish_.rebuild(cfg);
        built_fish_enabled_ = cfg.fish.enabled;
        built_school_count_ = cfg.fish.school_count;
        built_per_school_   = cfg.fish.per_school;
        built_fish_seed_    = cfg.fish.seed;
        built_fish_spread_  = cfg.fish.spread_m;
    }
}

void Ecosystem::update(const Config& cfg, float dt, float t, const HeightFn& water_height) {
    if (cfg.entity.boat_enabled) {
        float half = 0.5f * cfg.voxel.grid_extent * cfg.voxel.voxel_size_m;
        boat_.update(dt, t, water_height, cfg.entity.boat_speed_mps, half, cfg.voxel.voxel_size_m);
    }
    kelp_.update(cfg, t, water_height);
    fish_.update(cfg, dt, t, water_height,
                 [&](float x, float z) { return floor_top_y(cfg, x, z); });
}

void Ecosystem::build_stamp(const Config& cfg, const VoxelWorld& w, StampList& out) const {
    out.clear();
    kelp_.build_stamp(cfg, w, out);
    fish_.build_stamp(cfg, w, out);
    if (cfg.entity.boat_enabled) {
        auto cells = boat_cells(boat_.state(), w);
        for (uint32_t c : cells) out.push(c, VoxMat::Boat);   // boat last: wins overlaps
    }
}

bool Ecosystem::shed_boat_wake(const Config& cfg, glm::vec2& out_world) {
    if (!cfg.entity.boat_enabled) return false;
    return boat_.shed_wake(cfg.voxel.voxel_size_m, out_world);
}

float Ecosystem::floor_top_y(const Config& cfg, float x, float z) const {
    int extent = cfg.voxel.grid_extent;
    if (floor_.empty()) return -cfg.voxel.base_depth_m;
    float half = 0.5f * extent * cfg.voxel.voxel_size_m;
    int ix = std::clamp((int)std::floor((x + half) / cfg.voxel.voxel_size_m), 0, extent - 1);
    int iz = std::clamp((int)std::floor((z + half) / cfg.voxel.voxel_size_m), 0, extent - 1);
    int h = floor_[(size_t)iz * extent + ix].height;
    return -cfg.voxel.base_depth_m + (float)h * cfg.voxel.height_step_m;
}
}
