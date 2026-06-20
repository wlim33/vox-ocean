#pragma once
#include "voxel/VoxelWorld.h"
#include "voxel/FloorGen.h"
#include "voxel/MaterialCa.h"
#include "entity/StampBudget.h"
#include "world/EditList.h"
#include <optional>
#include <vector>
#include <cstdint>
namespace vox {
struct Config;

// CPU-authoritative material world. Owns ONE dense grid (material_: terrain +
// dynamic sand, no entities), evolved in place by a Margolus CA. Entities are a
// sparse GPU-render overlay composited only into the emitted EditList, never into
// material_. Metal-free.
class World {
public:
    void configure(const Config& cfg);   // (re)gen terrain + seed sand on param/seed change
    // One sparse per-frame pass: step the CA, composite the entity overlay, emit
    // the EditList (one entry per dirty cell). Sets out.resync after configure().
    void step(const Config& cfg, float dt, const StampList& entities, EditList& out);

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

    std::optional<VoxelWorld> grid_;
    std::vector<FloorColumn>  floor_;
    std::vector<uint8_t>      material_;      // THE dense grid (terrain + sand)
    MaterialCa                ca_;
    MaterialCaDims            dims_{0, 0};

    // sparse entity overlay
    std::vector<uint32_t>     overlay_cells_;      // current frame
    std::vector<uint8_t>      overlay_mats_;       // current frame (parallel to overlay_cells_)
    std::vector<uint32_t>     prev_overlay_cells_; // last frame
    mutable std::vector<uint8_t> composite_;       // materialize_composite scratch
    std::vector<uint32_t>     dirty_;              // step() scratch

    bool  resync_ = true;
    int   built_extent_ = -1, built_height_cells_ = -1, built_seed_ = 0;
    float built_base_depth_ = -1.0f, built_height_step_ = -1.0f, built_voxel_size_ = -1.0f;
    bool  built_sand_enabled_ = false; int built_sand_radius_ = -1, built_sand_thick_ = -1;
};
}
