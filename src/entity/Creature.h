// src/entity/Creature.h
#pragma once
#include <glm/glm.hpp>
#include <cstdint>
namespace vox {

enum SpeciesId : uint16_t { Species_None = 0, Species_Minnow = 1, Species_Predator = 2 };

// One published creature body, snapshotted for inter-creature sensing.
struct CreaturePresence {
    glm::vec3 pos {0.0f, 0.0f, 0.0f};
    float     yaw = 0.0f;
    uint16_t  species_id = 0;
    uint16_t  trait_bits = 0;   // packed per-instance traits (e.g. size class)
};

}
