#include "voxel/FloorGen.h"
#include "voxel/VoxelWorld.h"
#include <algorithm>
#include <cmath>
namespace vox {
namespace {
// Integer mix -> [0,1). Deterministic across platforms (no std::rand).
float hash01(uint32_t x, uint32_t z, uint32_t seed) {
    uint32_t h = x * 0x8da6b343u + z * 0xd8163841u + seed * 0xcb1ab31fu;
    h ^= h >> 13; h *= 0x7feb352du; h ^= h >> 15;
    return (float)(h & 0xffffffu) / 16777216.0f;
}
// 2D value noise: smoothstep-faded bilinear blend of lattice hashes.
float value_noise(float x, float z, uint32_t seed) {
    int xi = (int)std::floor(x), zi = (int)std::floor(z);
    float xf = x - xi, zf = z - zi;
    float u = xf * xf * (3.0f - 2.0f * xf);
    float v = zf * zf * (3.0f - 2.0f * zf);
    float a = hash01(xi, zi, seed),     b = hash01(xi + 1, zi, seed);
    float c = hash01(xi, zi + 1, seed), d = hash01(xi + 1, zi + 1, seed);
    float top = a + (b - a) * u, bot = c + (d - c) * u;
    return top + (bot - top) * v;
}
}
std::vector<FloorColumn> generate_floor(const FloorParams& p) {
    std::vector<FloorColumn> out((size_t)p.extent * p.extent);
    // Hills occupy at most the bottom third of the grid, so water always has
    // head-room above the tallest dune (pinned by floor_gen_test).
    int max_h = std::max(2, p.height_cells / 3);
    for (int iz = 0; iz < p.extent; ++iz)
        for (int ix = 0; ix < p.extent; ++ix) {
            float fx = ix * (8.0f / p.extent), fz = iz * (8.0f / p.extent);
            float n = 0.60f * value_noise(fx,        fz,        p.seed)
                    + 0.30f * value_noise(fx * 2.0f, fz * 2.0f, p.seed + 1)
                    + 0.10f * value_noise(fx * 4.0f, fz * 4.0f, p.seed + 2);
            int h = 1 + (int)(n * (float)(max_h - 1));
            // Rock outcrops break through on a sparser independent channel.
            float rock = value_noise(fx * 1.5f, fz * 1.5f, p.seed + 99);
            uint8_t mat = (uint8_t)(rock > 0.72f ? VoxMat::Rock : VoxMat::Sand);
            out[(size_t)iz * p.extent + ix] = { (uint8_t)std::clamp(h, 1, max_h), mat };
        }
    return out;
}
}
