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

// M2 see-through walk, CPU mirror of voxel_march.metal march_fs — keep in
// lockstep. Phase 1: march to the first non-Air cell. If it is Water:
// record the interface, bend the ray ONCE (refract, entering eta = 1/ior),
// then transmit through the volume accumulating the metric distance spent
// inside Water cells, until an opaque cell, the grid edge, or the step
// budget ends the walk. Opaque = any material that is not Air or Water.
struct TransmitResult {
    bool  hit = false;            // ended on an opaque cell
    int   ix = -1, iy = -1, iz = -1;   // that opaque cell
    int   opaque_axis = -1;       // face axis the opaque cell was entered through
    int   entry_axis = -1;        // axis of the FIRST water interface; -1 = never touched water
    float entry_t = 0.0f;         // ray t at that interface (pre-bend parameterization)
    float water_dist = 0.0f;      // metric distance traveled inside Water cells
    bool  exited_up = false;      // left the grid moving upward (sky behind the water)
    int   steps = 0;
};
TransmitResult dda_march_transmit(glm::vec3 origin, glm::vec3 dir,
                                  const VoxelWorld& world, const uint8_t* materials,
                                  int max_steps, float ior);
}
