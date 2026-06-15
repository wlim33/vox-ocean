#pragma once
#include "voxel/VoxelWorld.h"
#include "entity/Wander.h"
#include "entity/StampBudget.h"
#include <functional>
#include <vector>
#include <glm/glm.hpp>
namespace vox {
struct Config;

struct Fish {
    glm::vec3 pos {0.0f, 0.0f, 0.0f};   // world (x, y, z)
    float yaw = 0.0f;
    bool  visible = true;               // false where water over the floor is too shallow
};

// Several schools: each centroid wanders deterministically (shared Wander);
// members hold formation by seeded offsets and trail at a mid-water depth.
class FishSchools {
public:
    using HeightFn = std::function<float(float, float)>;   // surface y at (x,z)
    using FloorFn  = std::function<float(float, float)>;    // floor-top y at (x,z)
    void rebuild(const Config& cfg);
    void update(const Config& cfg, float dt, float t,
                const HeightFn& surface, const FloorFn& floor_fn);
    void build_stamp(const Config& cfg, const VoxelWorld& w, StampList& out) const;
    const std::vector<Fish>& fish() const { return fish_; }
private:
    std::vector<WanderState> centroids_;   // one per school
    std::vector<glm::vec2>   offset_;      // per-fish school-local formation offset
    std::vector<int>         school_of_;   // per-fish school index
    std::vector<float>       bob_phase_;   // per-fish vertical bob phase
    std::vector<Fish>        fish_;        // resolved world fish, refreshed each update
};
}
