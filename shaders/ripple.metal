#include <metal_stdlib>
#include "shader_types.h"
using namespace metal;

// Damped 2D wave equation, one fixed 1/60s Verlet step per frame.
// Mirror of src/voxel/Ripple.cpp ripple_step — keep in lockstep.
// Splashes are added to the freshly computed step so a drop lands and
// propagates starting next frame.
kernel void ripple_step(
    constant RippleUniforms& U                     [[buffer(0)]],
    const device RippleSplash* splashes            [[buffer(1)]],
    texture2d<float, access::read>  prev           [[texture(0)]],
    texture2d<float, access::read>  cur            [[texture(1)]],
    texture2d<float, access::write> next           [[texture(2)]],
    uint2 gid [[thread_position_in_grid]])
{
    if ((int)gid.x >= U.grid_extent || (int)gid.y >= U.grid_extent) return;
    int x = (int)gid.x, z = (int)gid.y;
    int xm = max(x - 1, 0), xp = min(x + 1, U.grid_extent - 1);
    int zm = max(z - 1, 0), zp = min(z + 1, U.grid_extent - 1);

    float c = cur.read(uint2(x, z)).x;
    float lap = cur.read(uint2(xm, z)).x + cur.read(uint2(xp, z)).x
              + cur.read(uint2(x, zm)).x + cur.read(uint2(x, zp)).x - 4.0f * c;
    float h = U.damping * (2.0f * c - prev.read(uint2(x, z)).x + U.k * lap);

    // Absorbing border band (outer 8 cells), mirror of Ripple.cpp.
    int edge = min(min(x, U.grid_extent - 1 - x), min(z, U.grid_extent - 1 - z));
    if (edge < 8) h *= 0.90f + 0.10f * (float)edge / 8.0f;

    // Gaussian splash deposits for this frame.
    int n = min(U.splash_count, MAX_SPLASHES);
    for (int i = 0; i < n; ++i) {
        float dx = (float)x - splashes[i].x;
        float dz = (float)z - splashes[i].z;
        float r2 = splashes[i].radius * splashes[i].radius;
        h += splashes[i].amp * exp(-(dx * dx + dz * dz) / max(r2, 1e-3f));
    }
    next.write(float4(h, 0.0f, 0.0f, 0.0f), uint2(x, z));
}
