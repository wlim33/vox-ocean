#include "entity/Ecosystem.h"
#include "world/World.h"
#include "core/Config.h"
namespace vox {

void Ecosystem::rebuild_if_dirty(const Config& cfg, const World& world) {
    bool floor_dirty = cfg.voxel.grid_extent != built_extent_
                    || cfg.voxel.height_cells != built_height_cells_
                    || cfg.voxel.floor_seed   != built_floor_seed_
                    || cfg.voxel.base_depth_m  != built_floor_base_depth_
                    || cfg.voxel.height_step_m != built_floor_step_;
    if (floor_dirty) {
        built_extent_            = cfg.voxel.grid_extent;
        built_height_cells_      = cfg.voxel.height_cells;
        built_floor_seed_        = cfg.voxel.floor_seed;
        built_floor_base_depth_  = cfg.voxel.base_depth_m;
        built_floor_step_        = cfg.voxel.height_step_m;
    }
    if (floor_dirty
        || cfg.kelp.enabled      != built_kelp_enabled_
        || cfg.kelp.density      != built_kelp_density_
        || cfg.kelp.max_height_m != built_kelp_height_
        || cfg.kelp.seed         != built_kelp_seed_
        || cfg.voxel.height_step_m != built_kelp_step_) {
        kelp_.rebuild(cfg, world.floor());
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

void Ecosystem::add_creature(std::unique_ptr<ICreature> c) {
    creatures_.push_back(std::move(c));
}

void Ecosystem::update(const Config& cfg, float dt, float t, const HeightFn& water_height,
                       const World& world) {
    if (cfg.entity.boat_enabled) {
        float half = 0.5f * cfg.voxel.grid_extent * cfg.voxel.voxel_size_m;
        boat_.update(dt, t, water_height, cfg.entity.boat_speed_mps, half, cfg.voxel.voxel_size_m);
    }
    kelp_.update(cfg, t, water_height);
    fish_.update(cfg, dt, t, water_height,
                 [&](float x, float z) { return world.floor_top_y(x, z); });
    // --- creatures: publish all presences, THEN decide against the snapshot ---
    registry_.clear();
    for (auto& c : creatures_) c->publish_presence(registry_);
    const VoxelWorld& g = world.grid();
    for (auto& c : creatures_) {
        CreatureCtx ctx{ cfg, dt, t, world, g, water_height, registry_ };
        c->update(ctx);
    }
}

void Ecosystem::build_stamp(const Config& cfg, const VoxelWorld& w, StampList& out) {
    out.clear();
    creature_edits_.idx.clear();
    creature_edits_.mat.clear();
    creature_edits_.resync = false;
    kelp_.build_stamp(cfg, w, out);
    CreatureActs acts{ out, creature_edits_ };
    for (auto& c : creatures_) c->act(w, acts);   // creatures between kelp and fish/boat
    fish_.build_stamp(cfg, w, out);
    if (cfg.entity.boat_enabled) {
        auto cells = boat_cells(boat_.state(), w);
        for (uint32_t c : cells) out.push(c, VoxMat::Boat);   // boat last: wins overlaps
    }
}

}

