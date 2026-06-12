#pragma once
#include "voxel/VoxelWorld.h"
#include <cstdint>
#include <glm/glm.hpp>
namespace vox {
// CPU mirror of the DDA traversal in shaders/voxel_march.metal — keep in
// lockstep. Marches from the ray's entry into the grid AABB until it hits a
// non-Air cell, leaves the grid, or exceeds max_steps.
struct DdaHit {
    bool  hit = false;
    int   ix = -1, iy = -1, iz = -1;  // hit cell
    int   face_axis = -1;             // axis of the face the ray entered through: 0=x 1=y 2=z
    float t = 0.0f;                   // ray parameter at the hit cell's entry
    int   steps = 0;
};
DdaHit dda_march(glm::vec3 origin, glm::vec3 dir, const VoxelWorld& world,
                 const uint8_t* materials, int max_steps);
}
