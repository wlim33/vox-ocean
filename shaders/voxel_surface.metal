#include <metal_stdlib>
#include "shader_types.h"
using namespace metal;

struct VoxVertexIn { float3 pos [[attribute(0)]]; float3 normal [[attribute(1)]]; };
struct VoxVOut {
    float4 pos [[position]];
    float3 world_pos;
    float3 normal;
    float  fold_min;
    float  top_y;
};

vertex VoxVOut voxel_vs(
    VoxVertexIn vin                       [[stage_in]],
    constant CameraUniforms& cam          [[buffer(1)]],
    constant VoxelSurfaceUniforms& S      [[buffer(2)]],
    const device VoxelInstance* inst      [[buffer(3)]],
    uint iid [[instance_id]])
{
    int ix = (int)(iid % (uint)S.grid_extent);
    int iz = (int)(iid / (uint)S.grid_extent);
    float half_patch = 0.5 * S.grid_extent * S.voxel_size_m;
    VoxelInstance I = inst[iid];

    float3 wp;
    wp.x = ix * S.voxel_size_m - half_patch + vin.pos.x * S.voxel_size_m;
    wp.z = iz * S.voxel_size_m - half_patch + vin.pos.z * S.voxel_size_m;
    wp.y = mix(-S.base_depth_m, I.top_y, vin.pos.y);   // unit column stretched to height

    VoxVOut o;
    o.world_pos = wp;
    o.pos = cam.view_proj * float4(wp, 1.0);
    o.normal = vin.normal;
    o.fold_min = I.fold_min;
    o.top_y = I.top_y;
    return o;
}

static float3 aces_tonemap(float3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

fragment float4 voxel_fs(
    VoxVOut in                            [[stage_in]],
    constant CameraUniforms& cam          [[buffer(1)]],
    constant VoxelSurfaceUniforms& S      [[buffer(2)]],
    texturecube<float> sky_cube           [[texture(0)]])
{
    constexpr sampler cube_smp(filter::linear, address::clamp_to_edge);
    float3 n = in.normal;
    float3 V = normalize(cam.position - in.world_pos);
    float3 sun = normalize(S.sun_dir);
    float3 color;

    if (n.y > 0.5) {
        // Top face: water optics, flat-shaded per voxel.
        float  nv = max(dot(n, V), 0.0);
        float  F  = 0.02 + 0.98 * pow(1.0 - nv, 5.0);
        float3 R  = reflect(-V, n);
        float3 reflection = sky_cube.sample(cube_smp, R).rgb
                          + S.sun_color * pow(max(dot(R, sun), 0.0), S.sun_shininess);
        float3 refraction = S.deep_water_color;
        color = mix(refraction, reflection, F);
        float foam = saturate(S.foam_strength * (S.foam_threshold - in.fold_min));
        color = mix(color, float3(1.0), foam);
    } else {
        // Side face: Beer-Lambert depth-tinted water volume down to the
        // diorama base, with a cheap sun-facing lambert so the wall reads
        // as a surface (project decision: Beer-Lambert falloff).
        float depth = in.top_y - in.world_pos.y;
        float3 absorb = exp(-S.depth_fog_density * depth * S.extinction_rgb);
        float lambert = 0.35 + 0.65 * max(dot(n, sun), 0.0);
        color = S.deep_water_color * absorb * lambert;
        // Foam bleeds one step down from a foamy crest.
        float foam = saturate(S.foam_strength * (S.foam_threshold - in.fold_min));
        float crest = saturate(1.0 - depth / max(S.voxel_size_m, 1e-3));
        color = mix(color, float3(0.9), foam * crest);
    }
    return float4(aces_tonemap(color), 1.0);
}
