#pragma once
#include <glm/glm.hpp>
namespace vox {
// Minimal camera payload the renderers consume. ortho_backup is 0 for a
// perspective camera; for orthographic it is the world distance to push each
// per-pixel ray origin back along -dir so the DDA starts outside the grid AABB
// (parallel rays would otherwise collapse to one if given a single origin).
struct CameraView {
    glm::mat4 view_proj{1.0f};
    glm::vec3 position{0.0f};
    float     ortho_backup = 0.0f;
};
}
