#pragma once
#include "voxel/VoxelWorld.h"      // VoxelWorld, VoxMat
#include "voxel/FloorGen.h"        // FloorColumn
#include "entity/StampBudget.h"    // StampList
#include "world/EditList.h"
#include <optional>
#include <vector>
#include <cstdint>
namespace vox {
struct Config;

// CPU-authoritative discrete world: owns the procedural terrain and a dense
// per-cell material grid (cells_) that mirrors the GPU's discrete voxels 1:1.
// Entities are composited on top each frame via ingest(); downstream consumers
// read cells() (the GPU stamp list today, an EditList diff in a later step).
// Metal-free.
class World {
public:
    // (Re)generate terrain + (re)allocate the grid when grid params/seed change.
    // No-op when nothing changed.
    void configure(const Config& cfg);
    // Reset the per-frame overlay: cells_ = terrain_.
    void begin_frame();
    // Composite an entity stamp list onto cells_ (last writer wins per cell).
    void ingest(const StampList& stamps);
    // Produce the per-frame delta from the previous frame's cells_ to the
    // current. Sets out.resync (empty) on the first frame after configure().
    void build_edits(EditList& out);

    // Terrain (static) -------------------------------------------------------
    const std::vector<uint8_t>& terrain_cells() const { return terrain_; }
    const std::vector<FloorColumn>& floor() const { return floor_; }
    float floor_top_y(float x, float z) const;

    // Discrete grid (terrain + this frame's entities) ------------------------
    const std::vector<uint8_t>& cells() const { return cells_; }
    const VoxelWorld& grid() const { return *grid_; }
    bool configured() const { return grid_.has_value(); }

private:
    std::optional<VoxelWorld> grid_;       // indexing math (no default ctor)
    std::vector<FloorColumn>  floor_;       // procedural floor (terrain truth)
    std::vector<uint8_t>      terrain_;     // dense static terrain materials
    std::vector<uint8_t>      cells_;       // dense composited discrete grid
    std::vector<uint8_t>      prev_cells_;  // previous frame's cells_ (diff baseline)
    bool                      resync_ = true; // true until first build_edits after configure
    int   built_extent_ = -1, built_height_cells_ = -1, built_seed_ = 0;
    float built_base_depth_ = -1.0f, built_height_step_ = -1.0f, built_voxel_size_ = -1.0f;
};
}
