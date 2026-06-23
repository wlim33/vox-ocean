#pragma once
#include "voxel/VoxelWorld.h"
#include "entity/Wander.h"
#include "entity/StampBudget.h"
#include "entity/Creature.h"
#include <utility>
#include <vector>
#include <glm/glm.hpp>
namespace vox {
struct Config;

struct Fish {
    glm::vec3 pos {0.0f, 0.0f, 0.0f};
    float yaw = 0.0f;
    bool  visible = true;
};

class FishSchools : public ICreature {
public:
    explicit FishSchools(uint16_t species_id = Species_Minnow, uint16_t size_class = 0)
        : species_(species_id), size_class_(size_class) {}

    void rebuild(const Config& cfg, const World& world) override;
    uint16_t species_id() const override { return species_; }
    void publish_presence(CreatureRegistry& reg) const override;
    void update(const CreatureCtx& ctx) override;
    void act(const VoxelWorld& grid, CreatureActs& out) const override;

    const std::vector<Fish>& fish() const { return fish_; }
    const std::vector<float>& boldness_for_test() const { return boldness_; }
private:
    // Combine world-derived steering vectors into one heading nudge (per school).
    static glm::vec2 steer(glm::vec2 fwd, glm::vec2 hazard, glm::vec2 food,
                           glm::vec2 flock, glm::vec2 avoid, float boldness);
    uint16_t species_ = Species_Minnow;
    uint16_t size_class_ = 0;
    std::vector<WanderState> centroids_;
    std::vector<glm::vec2>   offset_;
    std::vector<int>         school_of_;
    std::vector<float>       bob_phase_;
    std::vector<float>       boldness_;
    std::vector<float>       school_boldness_;   // per-school mean boldness, precomputed in rebuild
    std::vector<Fish>        fish_;
    mutable std::vector<std::pair<uint32_t,uint8_t>> pending_edits_;  // kelp eaten this frame
};
}
