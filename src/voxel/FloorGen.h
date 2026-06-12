#pragma once
#include <cstdint>
#include <vector>
namespace vox {
// Deterministic procedural ocean floor. Pure C++, Metal-free; runs once at
// init and on voxel-grid rebuilds, then is expanded into the terrain texture.
struct FloorParams {
    int      extent;        // columns per side
    int      height_cells;  // world-grid vertical cells (hills use the bottom third)
    uint32_t seed;
};
struct FloorColumn {
    uint8_t height;    // terrain cells above the diorama base, >= 1
    uint8_t material;  // (uint8_t)VoxMat::Sand or VoxMat::Rock
};
// extent*extent columns, x fastest (index = iz * extent + ix).
std::vector<FloorColumn> generate_floor(const FloorParams& p);
}
