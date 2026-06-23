// src/entity/Creature.h
#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include "voxel/VoxelWorld.h"     // VoxMat, VoxelWorld
#include "entity/StampBudget.h"   // StampList
#include "world/EditList.h"       // EditList
#include <functional>
#include <optional>
namespace vox {

enum SpeciesId : uint16_t { Species_None = 0, Species_Minnow = 1, Species_Predator = 2 };

// One published creature body, snapshotted for inter-creature sensing.
struct CreaturePresence {
    glm::vec3 pos {0.0f, 0.0f, 0.0f};
    float     yaw = 0.0f;
    uint16_t  species_id = 0;
    uint16_t  trait_bits = 0;   // packed per-instance traits (e.g. size class)
};

struct Config;
class World;
class CreatureRegistry;

// Two write channels handed to act(): transient body + durable world edits.
struct CreatureActs {
    StampList& occupancy;   // per-frame overlay (recomposited each frame)
    EditList&  edits;       // durable material_ changes (via apply_user_edit)
};

// Read-only perception window for one tick: world cells + neighbor registry.
struct CreatureCtx {
    const Config&     cfg;
    float             dt;
    float             t;
    const World&      world;
    const VoxelWorld& grid;
    std::function<float(float,float)> water_surface;   // surface y at (x,z)
    const CreatureRegistry& neighbors;

    VoxMat sample(float x, float y, float z) const;     // Air if out-of-grid
    float  floor_top_y(float x, float z) const;
    std::optional<glm::vec3> find_nearest(
        glm::vec3 from, float radius,
        const std::function<bool(VoxMat)>& pred) const;
    void for_each_neighbor(
        glm::vec3 from, float radius,
        const std::function<void(const CreaturePresence&)>& fn) const;
};

// One species/population of living creatures. The manager IS the creature
// (Approach A) — one virtual call per species per frame over its SoA arrays.
class ICreature {
public:
    virtual ~ICreature() = default;
    virtual void rebuild(const Config& cfg, const World& world) = 0;
    virtual uint16_t species_id() const = 0;
    virtual void publish_presence(CreatureRegistry& reg) const = 0;
    virtual void update(const CreatureCtx& ctx) = 0;
    virtual void act(const VoxelWorld& grid, CreatureActs& out) const = 0;
};

}
