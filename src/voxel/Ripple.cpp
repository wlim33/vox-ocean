#include "voxel/Ripple.h"
#include <algorithm>
namespace vox {

float ripple_k(float wave_speed_mps, float dt_s, float dx_m) {
    float r = wave_speed_mps * dt_s / dx_m;
    return std::min(r * r, 0.45f);
}

void ripple_step(const std::vector<float>& prev, const std::vector<float>& cur,
                 std::vector<float>& next, int extent, float k, float damping) {
    auto idx = [extent](int x, int z) { return (size_t)z * extent + x; };
    for (int z = 0; z < extent; ++z)
        for (int x = 0; x < extent; ++x) {
            int xm = std::max(x - 1, 0), xp = std::min(x + 1, extent - 1);
            int zm = std::max(z - 1, 0), zp = std::min(z + 1, extent - 1);
            float c = cur[idx(x, z)];
            float lap = cur[idx(xm, z)] + cur[idx(xp, z)]
                      + cur[idx(x, zm)] + cur[idx(x, zp)] - 4.0f * c;
            float h = damping * (2.0f * c - prev[idx(x, z)] + k * lap);
            // Absorbing border band: ramp damping over the outer 8 cells so
            // the diorama wall doesn't reflect wakes back into the scene.
            int edge = std::min(std::min(x, extent - 1 - x),
                                std::min(z, extent - 1 - z));
            if (edge < 8) h *= 0.90f + 0.10f * (float)edge / 8.0f;
            next[idx(x, z)] = h;
        }
}
}
