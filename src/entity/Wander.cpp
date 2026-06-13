#include "entity/Wander.h"
#include <algorithm>
#include <cmath>
namespace vox {
float wander_step(WanderState& s, float dt, float t, float speed_mps,
                  float patch_half_m) {
    float yaw_rate = 0.4f * (0.6f * std::sin(t * 0.23f) + 0.3f * std::sin(t * 0.71f));

    float r = glm::length(s.pos);
    if (r > 0.6f * patch_half_m) {
        glm::vec2 to_center = -s.pos / std::max(r, 1e-3f);
        float want = std::atan2(to_center.y, to_center.x);
        float diff = want - s.yaw;
        while (diff >  3.1415927f) diff -= 6.2831853f;
        while (diff < -3.1415927f) diff += 6.2831853f;
        float urgency = std::min((r - 0.6f * patch_half_m) / (0.25f * patch_half_m), 1.0f);
        yaw_rate = glm::mix(yaw_rate, std::clamp(diff, -1.2f, 1.2f), urgency);
    }
    s.yaw += yaw_rate * dt;
    glm::vec2 fwd { std::cos(s.yaw), std::sin(s.yaw) };
    float travel = speed_mps * dt;
    s.pos += fwd * travel;
    return travel;
}
}
