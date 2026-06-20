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

// Material IDs stored in the world grid (r8Uint texture3d).
// Lockstep mirror of vox::VoxMat in src/voxel/VoxelWorld.h.
#define MAT_AIR   0
#define MAT_WATER 1
#define MAT_SAND  2
#define MAT_ROCK  3
#define MAT_BOAT  4
#define MAT_KELP  5
#define MAT_FISH  6
#define MAT_SANDGRAIN 7
#define NUM_MATERIALS 8

struct ApplyEditsUniforms {
    int grid_extent;
    int height_cells;
    int edit_count;
    int _pad;
};

struct MarchUniforms {
    mat4  inv_view_proj;
    vec3  camera_pos;        float _mpad0;
    vec3  sun_dir;           float _mpad1;    // unit length (normalized CPU-side)
    vec3  sun_color;         float sun_shininess;
    vec3  deep_water_color;  float depth_fog_density;
    vec3  extinction_rgb;    float foam_threshold;
    int   grid_extent;       int height_cells; float voxel_size_m; float base_depth_m;
    int   max_steps;         float water_ior; float ortho_backup, _mpad4;
    // Each row above is exactly 16 bytes; palette[] relies on this for host/device layout parity.
    float foam_strength;     float height_step_m; float _mpad5, _mpad6;
    vec3  palette[NUM_MATERIALS];   // per-material albedo, uploaded from kMaterials
};
