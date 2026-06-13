#pragma once
#include <glm/glm.hpp>
namespace vox {
// Deterministic wandering steering shared by the boat and fish-school
// centroids: two incommensurate sines give a non-repeating course (no RNG, so
// trajectories reproduce in tests and benches); edge-avoidance blends toward
// the patch center past 60% of the half-extent, fully overriding at 85%.
struct WanderState {
    glm::vec2 pos {0.0f, 0.0f};
    float     yaw = 0.0f;
};

// Advance s by one step; returns the travel distance (speed_mps * dt).
float wander_step(WanderState& s, float dt, float t, float speed_mps,
                  float patch_half_m);
}
