#pragma once
#include "voxel/VoxelWorld.h"
#include <cstdint>
#include <functional>
#include <vector>
#include <glm/glm.hpp>
namespace vox {
// Autonomous boat: deterministic wandering sail (sine steering, no RNG —
// trajectories reproduce in tests and benches), heave-only buoyancy from a
// caller-supplied water-height function, stern wake emission.

// Hull occupancy in boat-local logical cells, voxel_size_m-sized: x along
// the length (bow at BOAT_LEN-1), y up from the keel, z across the beam.
inline constexpr int BOAT_LEN = 7, BOAT_HGT = 3, BOAT_BEAM = 3;
bool boat_hull(int x, int y, int z);

struct BoatState {
    glm::vec2 pos {0.0f, 0.0f};   // world xz
    float     yaw = 0.0f;          // radians, 0 = +x
    float     y   = 0.0f;          // hull-base world height (heave)
};

class Boat {
public:
    using HeightFn = std::function<float(float, float)>;   // world (x,z) -> water y
    void update(float dt, float t, const HeightFn& water_height,
                float speed_mps, float patch_half_m, float voxel_size_m);
    const BoatState& state() const { return state_; }
    glm::vec2 stern_world(float voxel_size_m) const;
    // Emit at most one stern wake impulse per ~voxel_size of travel, setting
    // out_world to the stern point. A wake sheds energy with DISTANCE, not
    // time: depositing every frame let a slow boat dig a self-reinforcing
    // ripple hole its own buoyancy then sank into. Dead in the water -> none.
    bool shed_wake(float voxel_size_m, glm::vec2& out_world);
private:
    BoatState state_;
    float heave_smooth_ = 0.0f;
    float wake_dist_ = 0.0f;       // unspent travel since the last wake impulse
    bool  primed_ = false;
};

// Rasterize the hull at state s into packed world-grid cell indices
// (VoxelWorld::cell_index layout). Iterates the boat's grid AABB and
// inverse-rotates each cell center into hull space — no rotation holes.
// Vertical: each logical hull layer spans round(voxel_size/height_step)
// grid layers, starting at the cell containing s.y.
std::vector<uint32_t> boat_cells(const BoatState& s, const VoxelWorld& w);
}
