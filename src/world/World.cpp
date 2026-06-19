#include "world/World.h"
#include "core/Config.h"
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
    if (grid_same && sand_same) return;

    if (!grid_same) {
        grid_.emplace(VoxelWorldParams{ v.grid_extent, v.height_cells, v.voxel_size_m,
                                        v.height_step_m, v.base_depth_m });
        floor_ = generate_floor({ v.grid_extent, v.height_cells, (uint32_t)v.floor_seed,
                                  v.base_depth_m, v.height_step_m });
        dims_ = { v.grid_extent, v.height_cells };
        int extent = v.grid_extent, hc = v.height_cells;
        terrain_top_.assign((size_t)extent * extent, 0);
        material_.assign((size_t)extent * extent * hc, (uint8_t)VoxMat::Air);
        for (int iz = 0; iz < extent; ++iz)
            for (int ix = 0; ix < extent; ++ix) {
                const FloorColumn& fc = floor_[(size_t)iz * extent + ix];
                int h = std::min((int)fc.height, hc);
                terrain_top_[(size_t)iz * extent + ix] = (uint8_t)std::min(h, 255);
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
    seed_sand(cfg);
    resync_ = true;
    prev_overlay_cells_.clear();

    built_extent_ = v.grid_extent; built_height_cells_ = v.height_cells;
    built_seed_ = v.floor_seed; built_base_depth_ = v.base_depth_m;
    built_height_step_ = v.height_step_m; built_voxel_size_ = v.voxel_size_m;
    built_sand_enabled_ = cfg.sand.enabled; built_sand_radius_ = cfg.sand.spawn_radius;
    built_sand_thick_ = cfg.sand.spawn_thickness;
}

void World::seed_sand(const Config& cfg) {
    if (!cfg.sand.enabled) return;
    int extent = dims_.extent, hc = dims_.height_cells;
    int cx = extent / 2, cz = extent / 2, r = std::min(cfg.sand.spawn_radius, extent / 2);
    int top = hc - 1, bot = std::max(0, hc - cfg.sand.spawn_thickness);
    for (int iz = std::max(0, cz - r); iz <= std::min(extent - 1, cz + r); ++iz)
        for (int ix = std::max(0, cx - r); ix <= std::min(extent - 1, cx + r); ++ix)
            for (int iy = bot; iy <= top; ++iy)
                material_[ca_cell_index(dims_, ix, iy, iz)] = (uint8_t)VoxMat::Sand;
    ca_.wake_box(cx - r, bot, cz - r, cx + r, top, cz + r);
}

const std::vector<uint8_t>& World::materialize_composite() const {
    composite_ = material_;
    for (size_t i = 0; i < overlay_cells_.size(); ++i) {
        uint32_t c = overlay_cells_[i];
        if (c < composite_.size()) composite_[c] = overlay_mats_[i];   // last writer wins
    }
    return composite_;
}

void World::step(const Config&, float, const StampList&, EditList& out) {
    out.clear();           // Task 5 implements the real pass
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
