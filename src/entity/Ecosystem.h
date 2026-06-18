#pragma once
#include "entity/Boat.h"
#include "entity/Kelp.h"
#include "entity/Fish.h"
#include "entity/StampBudget.h"
#include "voxel/VoxelWorld.h"
#include <functional>
#include <vector>
#include <glm/glm.hpp>
namespace vox {
struct Config;
class World;

// Owns every entity; the single entity surface the engine talks to.
// Terrain is read from World (owned by the Engine). Boat behavior is
// unchanged from M4 — only relocated.
class Ecosystem {
public:
    using HeightFn = std::function<float(float, float)>;   // world (x,z) -> water y
    // Rebuild kelp anchors when floor/kelp inputs change; reseed fish on
    // count/seed changes. Stateful entities (fish, boat) are NOT reset on
    // unrelated config changes.
    void rebuild_if_dirty(const Config& cfg, const World& world);
    // Advance boat + fish; refresh kelp sway. water_height = CPU WaterModel.
    void update(const Config& cfg, float dt, float t, const HeightFn& water_height,
                const World& world);
    // Append kelp, then fish, then boat (boat last -> wins overlaps).
    void build_stamp(const Config& cfg, const VoxelWorld& w, StampList& out) const;
    // Boat stern wake impulse for this frame (false if none / boat off).
    bool shed_boat_wake(const Config& cfg, glm::vec2& out_world);
private:
    Boat boat_;
    KelpBed kelp_;
    FishSchools fish_;
    int   built_extent_ = -1, built_height_cells_ = -1, built_floor_seed_ = 0;
    float built_floor_base_depth_ = -1.0f, built_floor_step_ = -1.0f;
    bool  built_kelp_enabled_ = false;
    float built_kelp_density_ = -1.0f, built_kelp_height_ = -1.0f;
    int   built_kelp_seed_ = 0;
    float built_kelp_step_ = -1.0f;
    bool  built_fish_enabled_ = false;
    int   built_school_count_ = -1, built_per_school_ = -1, built_fish_seed_ = 0;
    float built_fish_spread_ = -1.0f;
};
}
