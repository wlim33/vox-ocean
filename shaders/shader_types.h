#pragma once
#ifdef __METAL_VERSION__
#include <metal_stdlib>
using namespace metal;
#define ALIGN(x)
typedef float4x4 mat4;
typedef float3   vec3;
#else
#include <simd/simd.h>
#define ALIGN(x) alignas(x)
typedef simd_float4x4 mat4;
typedef simd_float3   vec3;
#endif

struct CameraUniforms {
    mat4  view;
    mat4  proj;
    mat4  view_proj;
    vec3  position;
    float _pad;
};

struct SkyUniforms {
    mat4  inv_view_proj;
    vec3  sun_dir;
    float turbidity;
    vec3  camera_pos;
    float _pad;
};

struct CascadeUniforms {
    int   N;
    float L;
    float t;
    float choppiness;
};

struct FftPassUniforms {
    int N;
    int direction; // 0 = horizontal, 1 = vertical
};

#define MAX_CASCADES 4

// Material IDs stored in the world grid (r8Uint texture3d).
// Lockstep mirror of vox::VoxMat in src/voxel/VoxelWorld.h.
#define MAT_AIR   0
#define MAT_WATER 1
#define MAT_SAND  2
#define MAT_ROCK  3
#define MAT_BOAT  4
#define MAT_KELP  5
#define MAT_FISH  6

struct WorldFillUniforms {
    int   grid_extent;
    int   height_cells;
    float voxel_size_m;
    float height_step_m;
    float base_depth_m;
    int   cascade_count;
    float ripple_foam, _wpad1;
    float cascade_size[MAX_CASCADES];
};

#define MAX_SPLASHES 64

struct RippleUniforms {
    int   grid_extent;
    float k;             // (c*dt/dx)^2, CFL-clamped CPU-side (ripple_k)
    float damping;
    int   splash_count;
};

struct RippleSplash {   // injected into the ripple field this frame
    float x, z;          // texel coords (column indices, fractional OK)
    float radius;        // texels
    float amp;           // meters; negative = depression (raindrop)
};

struct StampUniforms {
    int grid_extent;
    int height_cells;
    int count;
};

struct MarchUniforms {
    mat4  inv_view_proj;
    vec3  camera_pos;        float _mpad0;
    vec3  sun_dir;           float _mpad1;    // unit length (normalized CPU-side)
    vec3  sun_color;         float sun_shininess;
    vec3  deep_water_color;  float depth_fog_density;
    vec3  extinction_rgb;    float foam_threshold;
    vec3  sand_color;        float foam_strength;
    vec3  rock_color;        float height_step_m;
    int   grid_extent;       int height_cells; float voxel_size_m; float base_depth_m;
    int   max_steps;         float water_ior; float ortho_backup, _mpad4;
    vec3  boat_color;        float _mpad5;
    vec3  kelp_color;        float _mpad6;
    vec3  fish_color;        float _mpad7;
};
