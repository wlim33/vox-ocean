#include <metal_stdlib>
#include "shader_types.h"
using namespace metal;

struct MarchVOut { float4 pos [[position]]; float2 ndc; };

vertex MarchVOut march_vs(uint vid [[vertex_id]]) {
    const float2 verts[3] = { float2(-1,-3), float2(-1, 1), float2( 3, 1) };
    MarchVOut o;
    o.pos = float4(verts[vid], 0, 1);
    o.ndc = verts[vid];
    return o;
}

static float3 aces_tonemap(float3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Full-screen DDA through the material grid. CPU mirror: src/voxel/Dda.cpp —
// keep the traversal in lockstep. Misses write alpha 0 so the composite pass
// shows the sky underneath.
fragment float4 march_fs(
    MarchVOut in                          [[stage_in]],
    constant MarchUniforms& U             [[buffer(0)]],
    texture3d<uint, access::read> world   [[texture(0)]],
    texture2d<float> surface              [[texture(1)]],
    texturecube<float> sky_cube           [[texture(2)]])
{
    constexpr sampler cube_smp(filter::linear, address::clamp_to_edge);

    // Unproject this pixel's near/far points for the primary ray.
    float4 p0 = U.inv_view_proj * float4(in.ndc, 0.0, 1.0);
    float4 p1 = U.inv_view_proj * float4(in.ndc, 1.0, 1.0);
    float3 dir = normalize(p1.xyz / p1.w - p0.xyz / p0.w);
    float3 org = U.camera_pos;

    float half_patch = 0.5 * U.grid_extent * U.voxel_size_m;
    float3 bmin = float3(-half_patch, -U.base_depth_m, -half_patch);
    float3 bmax = float3( half_patch,
                          -U.base_depth_m + U.height_cells * U.height_step_m,
                           half_patch);
    float3 cell = float3(U.voxel_size_m, U.height_step_m, U.voxel_size_m);
    int3   dims = int3(U.grid_extent, U.height_cells, U.grid_extent);

    // Slab test (epsilon-pinned directions, mirrors Dda.cpp).
    float tmin = 0.0, tmax = 1e30;
    int enter_axis = -1;
    float3 d;
    for (int a = 0; a < 3; ++a) {
        d[a] = abs(dir[a]) > 1e-8 ? dir[a] : (dir[a] >= 0.0 ? 1e-8 : -1e-8);
        float t0 = (bmin[a] - org[a]) / d[a];
        float t1 = (bmax[a] - org[a]) / d[a];
        if (t0 > t1) { float tt = t0; t0 = t1; t1 = tt; }
        if (t0 > tmin) { tmin = t0; enter_axis = a; }
        tmax = min(tmax, t1);
    }
    if (tmin > tmax) return float4(0.0);   // miss: sky shows through

    float3 pos = org + dir * (tmin + 1e-4);
    int3 idx;
    for (int a = 0; a < 3; ++a)
        idx[a] = clamp((int)floor((pos[a] - bmin[a]) / cell[a]), 0, dims[a] - 1);

    int3   stp;
    float3 t_max, t_delta;
    for (int a = 0; a < 3; ++a) {
        stp[a]     = d[a] > 0.0 ? 1 : -1;
        float next = bmin[a] + (float)(idx[a] + (stp[a] > 0 ? 1 : 0)) * cell[a];
        t_max[a]   = (next - org[a]) / d[a];
        t_delta[a] = cell[a] / abs(d[a]);
    }

    int   axis  = enter_axis < 0 ? 1 : enter_axis;
    float t_cur = tmin;
    uint  mat   = MAT_AIR;
    for (int s = 0; s < U.max_steps; ++s) {
        mat = world.read(uint3(idx)).r;
        if (mat != MAT_AIR) break;
        axis = (t_max.x < t_max.y) ? (t_max.x < t_max.z ? 0 : 2)
                                   : (t_max.y < t_max.z ? 1 : 2);
        t_cur = t_max[axis];
        idx[axis] += stp[axis];
        if (idx[axis] < 0 || idx[axis] >= dims[axis]) return float4(0.0);
        t_max[axis] += t_delta[axis];
    }
    if (mat == MAT_AIR) return float4(0.0);   // step budget exhausted

    // Face normal comes free from the DDA step axis.
    float3 n = float3(0.0);
    n[axis] = d[axis] > 0.0 ? -1.0 : 1.0;
    float3 wp  = org + dir * t_cur;
    float3 V   = normalize(org - wp);
    float3 sun = U.sun_dir;   // unit length by contract (see shader_types.h)

    float4 surf = surface.read(uint2(idx.x, idx.z));   // (water top_y, fold_min)
    float  foam = saturate(U.foam_strength * (U.foam_threshold - surf.y));
    float3 color;

    if (mat == MAT_WATER) {
        if (axis == 1 && n.y > 0.5) {
            // Top face: water optics, flat-shaded per voxel (as v1).
            float  nv = max(dot(n, V), 0.0);
            float  F  = 0.02 + 0.98 * pow(1.0 - nv, 5.0);
            float3 R  = reflect(-V, n);
            float3 reflection = sky_cube.sample(cube_smp, R).rgb
                              + U.sun_color * pow(max(dot(R, sun), 0.0), U.sun_shininess);
            color = mix(U.deep_water_color, reflection, F);
            color = mix(color, float3(1.0), foam);
        } else {
            // Side face: Beer-Lambert depth tint (user decision), with the
            // one-step crest foam bleed carried over from v1.
            float depth = max(surf.x - wp.y, 0.0);
            float3 absorb = exp(-U.depth_fog_density * depth * U.extinction_rgb);
            float lambert = 0.35 + 0.65 * max(dot(n, sun), 0.0);
            color = U.deep_water_color * absorb * lambert;
            float crest = saturate(1.0 - depth / max(U.height_step_m, 1e-3));
            color = mix(color, float3(0.9), foam * crest);
        }
    } else {
        // Terrain: palette x lambert; tinted by the water column above when
        // submerged (visible at the diorama side walls and on islands' shores).
        float3 base = (mat == MAT_ROCK) ? U.rock_color : U.sand_color;
        float lambert = 0.35 + 0.65 * max(dot(n, sun), 0.0);
        color = base * lambert;
        float depth = surf.x - wp.y;
        if (depth > 0.0)
            color *= exp(-U.depth_fog_density * depth * U.extinction_rgb);
    }
    return float4(aces_tonemap(color), 1.0);
}

// Composite the (possibly lower-resolution) march target over the sky.
// Nearest filtering keeps voxel edges crisp when render_scale < 1.
struct CompVOut { float4 pos [[position]]; float2 uv; };

vertex CompVOut march_composite_vs(uint vid [[vertex_id]]) {
    const float2 verts[3] = { float2(-1,-3), float2(-1, 1), float2( 3, 1) };
    CompVOut o;
    o.pos = float4(verts[vid], 0, 1);
    // NDC +y is up; texture row 0 is the top -> flip y.
    o.uv  = float2(0.5 + 0.5 * verts[vid].x, 0.5 - 0.5 * verts[vid].y);
    return o;
}

fragment float4 march_composite_fs(CompVOut in [[stage_in]],
                                   texture2d<float> march_tex [[texture(0)]]) {
    constexpr sampler nearest(filter::nearest, address::clamp_to_edge);
    return march_tex.sample(nearest, in.uv);
}
