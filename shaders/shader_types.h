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

struct VoxelizeUniforms {
    int   grid_extent;
    float voxel_size_m;
    float height_step_m;
    float base_depth_m;
    int   cascade_count;
    float _vpad0, _vpad1, _vpad2;
    float cascade_size[MAX_CASCADES];
};

struct VoxelInstance {   // column xz derives from instance_id — keep this tiny
    float top_y;
    float fold_min;
};

struct VoxelSurfaceUniforms {
    int   grid_extent;
    float voxel_size_m;
    float base_depth_m;
    float _spad_a;          // pad to 16-byte boundary before vec3
    vec3  sun_dir;          float _spad0;   // sun_dir must be unit length (normalized CPU-side)
    vec3  sun_color;        float sun_shininess;
    vec3  deep_water_color; float depth_fog_density;
    vec3  extinction_rgb;   float foam_threshold;
    float foam_strength;    float height_step_m; float _spad2, _spad3;
};
