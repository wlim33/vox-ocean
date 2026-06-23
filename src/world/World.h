#pragma once
#include "voxel/VoxelWorld.h"
#include "voxel/FloorGen.h"
#include "voxel/MaterialCa.h"
#include "entity/StampBudget.h"
#include "world/EditList.h"
#include <optional>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "core/Config.h"
namespace vox {

// CPU-authoritative material world. Owns ONE dense grid (material_: terrain +
// water + dynamic sand, no entities), evolved in place by a Margolus CA. Entities are a
// sparse GPU-render overlay composited only into the emitted EditList, never into
// material_. Metal-free.
class World {
public:
    void configure(const Config& cfg);   // (re)gen terrain + seed sand on param/seed change
    // One sparse per-frame pass: step the CA, composite the entity overlay, emit
    // the EditList (one entry per dirty cell). Sets out.resync after configure().
    void step(const Config& cfg, float dt, const StampList& entities, EditList& out);

    // Write one cell into the authoritative grid (user edit), wake the CA around it,
    // and mark it dirty so the next step() emits it. Call BEFORE step() in a frame.
    void apply_user_edit(uint32_t cell, uint8_t mat);

    // Dense composite (material_ + current overlay) — materialised on demand for
    // GPU resync and the debug invariant only. Reuses an internal buffer.
    const std::vector<uint8_t>& materialize_composite() const;

    const std::vector<uint8_t>& material() const { return material_; }
    const std::vector<FloorColumn>& floor() const { return floor_; }
    float floor_top_y(float x, float z) const;
    const VoxelWorld& grid() const { return *grid_; }
    bool configured() const { return grid_.has_value(); }
    bool ca_awake() const { return ca_.awake(); }

private:
    void seed_water();
    void seed_sand(const Config& cfg);
    void seed_bubble(const Config& cfg);
    void seed_fire(const Config& cfg);
    void seed_lava(const Config& cfg);

    std::optional<VoxelWorld> grid_;
    std::vector<FloorColumn>  floor_;
    std::vector<uint8_t>      material_;      // THE dense grid (terrain + water + sand)
    MaterialCa                ca_;
    MaterialCaDims            dims_{0, 0};

    // sparse entity overlay
    std::vector<uint32_t>     overlay_cells_;      // current frame
    std::vector<uint8_t>      overlay_mats_;       // current frame (parallel to overlay_cells_)
    std::vector<uint32_t>     prev_overlay_cells_; // last frame
    mutable std::vector<uint8_t> composite_;       // materialize_composite scratch
    std::vector<uint32_t>     dirty_;              // step() scratch
    std::unordered_map<uint32_t, uint8_t> overlay_lut_;  // step() scratch: cell -> overlay mat (last wins)
    std::vector<uint32_t>     user_edited_;       // cells written by apply_user_edit this frame

    bool  resync_ = true;
    int   built_extent_ = -1, built_height_cells_ = -1, built_seed_ = 0;
    float built_base_depth_ = -1.0f, built_height_step_ = -1.0f, built_voxel_size_ = -1.0f;
    bool  built_sand_enabled_ = false; int built_sand_radius_ = -1, built_sand_thick_ = -1;
    bool  built_bubble_enabled_ = false; int built_bubble_radius_ = -1, built_bubble_depth_ = -1;
    FireConfig built_fire_{};
    LavaConfig built_lava_{};
};
}
