#include "world/World.h"
#include "core/Config.h"
#include "voxel/MaterialRegistry.h"
#include <algorithm>
#include <cmath>
namespace vox {

void World::configure(const Config& cfg) {
    const VoxelConfig& v = cfg.voxel;
    bool grid_same = grid_
        && v.grid_extent == built_extent_ && v.height_cells == built_height_cells_
        && v.floor_seed == built_seed_ && v.base_depth_m == built_base_depth_
        && v.height_step_m == built_height_step_ && v.voxel_size_m == built_voxel_size_;
    bool sand_same = cfg.sand.enabled == built_sand_enabled_
        && cfg.sand.spawn_radius == built_sand_radius_
        && cfg.sand.spawn_thickness == built_sand_thick_;
    bool bubble_same = cfg.bubble.enabled == built_bubble_enabled_
        && cfg.bubble.spawn_radius == built_bubble_radius_
        && cfg.bubble.spawn_depth == built_bubble_depth_;
    bool fire_same = cfg.fire.enabled == built_fire_.enabled
        && cfg.fire.spawn_radius == built_fire_.spawn_radius
        && cfg.fire.spawn_height == built_fire_.spawn_height
        && cfg.fire.burn_out_chance == built_fire_.burn_out_chance
        && cfg.fire.smoke_chance == built_fire_.smoke_chance
        && cfg.fire.smoke_dissipate_chance == built_fire_.smoke_dissipate_chance
        && cfg.fire.ignite_scale == built_fire_.ignite_scale
        && cfg.fire.boil_chance == built_fire_.boil_chance
        && cfg.fire.condense_chance == built_fire_.condense_chance;
    bool lava_same = cfg.lava.enabled == built_lava_.enabled
        && cfg.lava.spawn_radius == built_lava_.spawn_radius
        && cfg.lava.spawn_height == built_lava_.spawn_height
        && cfg.lava.cool_chance == built_lava_.cool_chance;
    if (grid_same && sand_same && bubble_same && fire_same && lava_same) return;

    if (!grid_same) {
        grid_.emplace(VoxelWorldParams{ v.grid_extent, v.height_cells, v.voxel_size_m,
                                        v.height_step_m, v.base_depth_m });
        floor_ = generate_floor({ v.grid_extent, v.height_cells, (uint32_t)v.floor_seed,
                                  v.base_depth_m, v.height_step_m });
        dims_ = { v.grid_extent, v.height_cells };
        int extent = v.grid_extent, hc = v.height_cells;
        material_.assign((size_t)extent * extent * hc, (uint8_t)VoxMat::Air);
        for (int iz = 0; iz < extent; ++iz)
            for (int ix = 0; ix < extent; ++ix) {
                const FloorColumn& fc = floor_[(size_t)iz * extent + ix];
                int h = std::min((int)fc.height, hc);
                for (int iy = 0; iy < h; ++iy)
                    material_[ca_cell_index(dims_, ix, iy, iz)] = fc.material;
            }
    } else {
        // Same grid, sand toggled: rebuild material_ from terrain to clear old sand.
        int extent = v.grid_extent, hc = v.height_cells;
        std::fill(material_.begin(), material_.end(), (uint8_t)VoxMat::Air);
        for (int iz = 0; iz < extent; ++iz)
            for (int ix = 0; ix < extent; ++ix) {
                const FloorColumn& fc = floor_[(size_t)iz * extent + ix];
                int h = std::min((int)fc.height, hc);
                for (int iy = 0; iy < h; ++iy)
                    material_[ca_cell_index(dims_, ix, iy, iz)] = fc.material;
            }
    }

    ca_.reset();
    seed_water();           // fill Water below sea level (equilibrium, no wake)
    seed_sand(cfg);
    seed_bubble(cfg);       // submerged gas blob (rises on wake)
    seed_fire(cfg);         // ignite a region (no-op when disabled)
    seed_lava(cfg);         // lava blob above the waterline (sinks on wake)
    if (cfg.fire.enabled || cfg.lava.enabled)
        ca_.enable_combustion((uint32_t)cfg.voxel.floor_seed,
                              { cfg.fire.burn_out_chance, cfg.fire.smoke_chance,
                                cfg.fire.smoke_dissipate_chance, cfg.fire.ignite_scale,
                                cfg.fire.boil_chance, cfg.fire.condense_chance,
                                cfg.lava.cool_chance });
    resync_ = true;
    prev_overlay_cells_.clear();

    built_extent_ = v.grid_extent; built_height_cells_ = v.height_cells;
    built_seed_ = v.floor_seed; built_base_depth_ = v.base_depth_m;
    built_height_step_ = v.height_step_m; built_voxel_size_ = v.voxel_size_m;
    built_sand_enabled_ = cfg.sand.enabled; built_sand_radius_ = cfg.sand.spawn_radius;
    built_sand_thick_ = cfg.sand.spawn_thickness;
    built_bubble_enabled_ = cfg.bubble.enabled; built_bubble_radius_ = cfg.bubble.spawn_radius;
    built_bubble_depth_ = cfg.bubble.spawn_depth;
    built_fire_ = cfg.fire;
    built_lava_ = cfg.lava;
}

void World::seed_water() {
    int extent = dims_.extent, hc = dims_.height_cells;
    int sea = std::min(grid_->water_top_cell(0.0f), hc);   // cells up to y=0
    for (int iz = 0; iz < extent; ++iz)
        for (int ix = 0; ix < extent; ++ix)
            for (int iy = 0; iy < sea; ++iy) {
                size_t i = ca_cell_index(dims_, ix, iy, iz);
                if (material_[i] == (uint8_t)VoxMat::Air) material_[i] = (uint8_t)VoxMat::Water;
            }
    // No wake_box: the fill is at equilibrium, so the CA stays asleep until disturbed.
}

void World::seed_sand(const Config& cfg) {
    if (!cfg.sand.enabled) return;
    int extent = dims_.extent, hc = dims_.height_cells;
    int cx = extent / 2, cz = extent / 2, r = std::min(cfg.sand.spawn_radius, extent / 2);
    int top = hc - 1, bot = std::max(0, hc - cfg.sand.spawn_thickness);
    for (int iz = std::max(0, cz - r); iz <= std::min(extent - 1, cz + r); ++iz)
        for (int ix = std::max(0, cx - r); ix <= std::min(extent - 1, cx + r); ++ix)
            for (int iy = bot; iy <= top; ++iy)
                material_[ca_cell_index(dims_, ix, iy, iz)] = (uint8_t)VoxMat::SandGrain;
    ca_.wake_box(cx - r, bot, cz - r, cx + r, top, cz + r);
}

void World::seed_bubble(const Config& cfg) {
    if (!cfg.bubble.enabled) return;
    int extent = dims_.extent, hc = dims_.height_cells;
    int cx = extent / 2, cz = extent / 2;
    int r  = std::min(cfg.bubble.spawn_radius, extent / 2);
    int cy = std::min(std::max(0, cfg.bubble.spawn_depth), hc - 1);
    int y0 = std::max(0, cy - r), y1 = std::min(hc - 1, cy + r);
    for (int iz = std::max(0, cz - r); iz <= std::min(extent - 1, cz + r); ++iz)
        for (int ix = std::max(0, cx - r); ix <= std::min(extent - 1, cx + r); ++ix)
            for (int iy = y0; iy <= y1; ++iy) {
                size_t i = ca_cell_index(dims_, ix, iy, iz);
                if (material_[i] == (uint8_t)VoxMat::Water)         // only seed into water → stays submerged
                    material_[i] = (uint8_t)VoxMat::Bubble;
            }
    ca_.wake_box(cx - r, y0, cz - r, cx + r, y1, cz + r);
}

void World::seed_fire(const Config& cfg) {
    if (!cfg.fire.enabled) return;
    int extent = dims_.extent, hc = dims_.height_cells;
    int cx = extent / 2, cz = extent / 2;
    int r  = std::min(cfg.fire.spawn_radius, extent / 2);
    int cy = std::min(std::max(0, cfg.fire.spawn_height), hc - 1);
    int y0 = std::max(0, cy - r), y1 = std::min(hc - 1, cy + r);
    for (int iz = std::max(0, cz - r); iz <= std::min(extent - 1, cz + r); ++iz)
        for (int ix = std::max(0, cx - r); ix <= std::min(extent - 1, cx + r); ++ix)
            for (int iy = y0; iy <= y1; ++iy) {
                size_t i = ca_cell_index(dims_, ix, iy, iz);
                uint8_t m = material_[i];
                if (m == (uint8_t)VoxMat::Air || material_props((VoxMat)m).flammability > 0.0f)
                    material_[i] = (uint8_t)VoxMat::Fire;   // ignite air + fuel only
            }
    ca_.wake_box(cx - r, y0, cz - r, cx + r, y1, cz + r);
}

void World::seed_lava(const Config& cfg) {
    if (!cfg.lava.enabled) return;
    int extent = dims_.extent, hc = dims_.height_cells;
    int cx = extent / 2, cz = extent / 2;
    int r  = std::min(cfg.lava.spawn_radius, extent / 2);
    int cy = std::min(std::max(0, cfg.lava.spawn_height), hc - 1);
    int y0 = std::max(0, cy - r), y1 = std::min(hc - 1, cy + r);
    for (int iz = std::max(0, cz - r); iz <= std::min(extent - 1, cz + r); ++iz)
        for (int ix = std::max(0, cx - r); ix <= std::min(extent - 1, cx + r); ++ix)
            for (int iy = y0; iy <= y1; ++iy) {
                size_t i = ca_cell_index(dims_, ix, iy, iz);
                if (material_[i] == (uint8_t)VoxMat::Air)        // seed into air only → sinks into the ocean
                    material_[i] = (uint8_t)VoxMat::Lava;
            }
    ca_.wake_box(cx - r, y0, cz - r, cx + r, y1, cz + r);
}

const std::vector<uint8_t>& World::materialize_composite() const {
    composite_ = material_;
    for (size_t i = 0; i < overlay_cells_.size(); ++i) {
        uint32_t c = overlay_cells_[i];
        if (c < composite_.size()) composite_[c] = overlay_mats_[i];   // last writer wins
    }
    return composite_;
}

void World::step(const Config& cfg, float /*dt*/, const StampList& entities, EditList& out) {
    out.clear();
    // Snapshot this frame's overlay (last-writer-wins per cell, append order).
    overlay_cells_.assign(entities.idx.begin(), entities.idx.end());
    overlay_mats_.assign(entities.mat.begin(), entities.mat.end());

    if (resync_) {
        out.resync = true;                          // consumer uploads materialize_composite()
        prev_overlay_cells_ = overlay_cells_;
        resync_ = false;
        return;
    }

    // 1. Advance the CA over material_ (dynamic sand only).
    std::vector<uint32_t> ca_changed;
    if (ca_.awake()) ca_.step(material_, dims_, ca_changed);

    // 2. Dirty union: CA changes ∪ last frame's overlay ∪ this frame's overlay.
    dirty_.clear();
    dirty_.insert(dirty_.end(), ca_changed.begin(), ca_changed.end());
    dirty_.insert(dirty_.end(), prev_overlay_cells_.begin(), prev_overlay_cells_.end());
    dirty_.insert(dirty_.end(), overlay_cells_.begin(), overlay_cells_.end());
    std::sort(dirty_.begin(), dirty_.end());
    dirty_.erase(std::unique(dirty_.begin(), dirty_.end()), dirty_.end());

    // 3. One composited edit per dirty cell: entity material if occupied, else material_.
    //    Build the current overlay lookup once (last writer wins).
    auto overlay_at = [&](uint32_t cell, uint8_t& mat) -> bool {
        bool hit = false;
        for (size_t i = 0; i < overlay_cells_.size(); ++i)
            if (overlay_cells_[i] == cell) { mat = overlay_mats_[i]; hit = true; }  // last wins
        return hit;
    };
    for (uint32_t cell : dirty_) {
        if (cell >= material_.size()) continue;     // ignore out-of-range overlay cells
        uint8_t mat;
        out.push(cell, overlay_at(cell, mat) ? mat : material_[cell]);
    }

    // 4. Roll the overlay forward.
    prev_overlay_cells_ = overlay_cells_;
}

float World::floor_top_y(float x, float z) const {
    if (!grid_.has_value()) return 0.0f;
    const VoxelWorldParams& p = grid_->params();
    if (floor_.empty()) return -p.base_depth_m;
    float half = 0.5f * p.extent * p.voxel_size_m;
    int ix = std::clamp((int)std::floor((x + half) / p.voxel_size_m), 0, p.extent - 1);
    int iz = std::clamp((int)std::floor((z + half) / p.voxel_size_m), 0, p.extent - 1);
    int h = floor_[(size_t)iz * p.extent + ix].height;
    return -p.base_depth_m + (float)h * p.height_step_m;
}
}
