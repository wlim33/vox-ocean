#pragma once
#include <cstdint>
#include <cmath>
#include <vector>
namespace vox {
// Deterministic procedural ocean floor. Pure C++, Metal-free; runs once at
// init and on voxel-grid rebuilds, then is expanded into the terrain texture.
struct FloorParams {
    int      extent;        // columns per side
    int      height_cells;  // world-grid vertical cells
    uint32_t seed;
    float    base_depth_m;  // diorama floor depth below y=0 (locates sea level)
    float    height_step_m; // vertical cell size (m)
};
struct FloorColumn {
    uint8_t height;    // terrain cells above the diorama base, >= 1
    uint8_t material;  // (uint8_t)VoxMat::Sand or VoxMat::Rock
};
// Still-water surface (world y=0) expressed as a cell count from the base.
inline int sea_level_cells(float base_depth_m, float height_step_m) {
    return (int)std::lround(base_depth_m / height_step_m);
}
// Minimum water cells above the floor for kelp/fish to inhabit a column.
constexpr int kMinDepthCells = 3;
// extent*extent columns, x fastest (index = iz * extent + ix).
std::vector<FloorColumn> generate_floor(const FloorParams& p);
}
