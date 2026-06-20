#include <metal_stdlib>
#include "shader_types.h"
#include "voxel_grid.h"
#include "dense_field.h"
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

// Restartable DDA core. CPU mirror: src/voxel/Dda.cpp (DdaState) — keep in
// lockstep. Restartability is what lets refraction re-aim the ray mid-walk.
struct DdaState {
    float3 d;
    int3   idx, stp;
    float3 t_max, t_delta;
    float  t_cur;
    int    axis;
    int    enter_axis;
};

struct GridDims { float3 bmin, bmax, cell; int3 dims; };

static bool dda_init(thread DdaState& S, float3 origin, float3 dir, GridDims G) {
    float tmin = 0.0, tmax = 1e30;
    S.enter_axis = -1;
    for (int a = 0; a < 3; ++a) {
        S.d[a] = abs(dir[a]) > 1e-8 ? dir[a] : (dir[a] >= 0.0 ? 1e-8 : -1e-8);
        float t0 = (G.bmin[a] - origin[a]) / S.d[a];
        float t1 = (G.bmax[a] - origin[a]) / S.d[a];
        if (t0 > t1) { float tt = t0; t0 = t1; t1 = tt; }
        if (t0 > tmin) { tmin = t0; S.enter_axis = a; }
        tmax = min(tmax, t1);
    }
    if (tmin > tmax) return false;

    float3 pos = origin + dir * (tmin + 1e-4);
    for (int a = 0; a < 3; ++a) {
        S.idx[a] = clamp((int)floor((pos[a] - G.bmin[a]) / G.cell[a]), 0, G.dims[a] - 1);
        S.stp[a] = S.d[a] > 0.0 ? 1 : -1;
        float next = G.bmin[a] + (float)(S.idx[a] + (S.stp[a] > 0 ? 1 : 0)) * G.cell[a];
        S.t_max[a]   = (next - origin[a]) / S.d[a];
        S.t_delta[a] = G.cell[a] / abs(S.d[a]);
    }
    S.axis  = S.enter_axis < 0 ? 1 : S.enter_axis;
    S.t_cur = tmin;
    return true;
}

static bool dda_step(thread DdaState& S, GridDims G) {
    S.axis = (S.t_max.x < S.t_max.y) ? (S.t_max.x < S.t_max.z ? 0 : 2)
                                     : (S.t_max.y < S.t_max.z ? 1 : 2);
    S.t_cur = S.t_max[S.axis];
    S.idx[S.axis] += S.stp[S.axis];
    if (S.idx[S.axis] < 0 || S.idx[S.axis] >= G.dims[S.axis]) return false;
    S.t_max[S.axis] += S.t_delta[S.axis];
    return true;
}

static float3 face_normal(int axis, float3 d) {
    float3 n = float3(0.0);
    n[axis] = d[axis] > 0.0 ? -1.0 : 1.0;
    return n;
}

static float3 terrain_color(uint mat, float3 n, float3 sun,
                            constant MarchUniforms& U) {
    float3 base = U.palette[mat];
    return base * (0.35 + 0.65 * max(dot(n, sun), 0.0));
}

// Material at a cell: the discrete-grid value. Water is now a real CA material
// in the grid (SP2-I), so no surface-height derivation is needed.
// CPU mirror: src/voxel/Dda.cpp — keep in lockstep.
static uint read_material(VoxelGridDesc vg,
                          texture3d<uint, access::read> discrete, int3 idx) {
    return vox_read(vg, discrete, idx);
}

// See-through water marcher: phase 1 march to first hit, phase 2 bend once
// at the water interface, phase 3 transmit accumulating Beer-Lambert path.
// CPU mirror of the walk: src/voxel/Dda.cpp dda_march_transmit — keep in
// lockstep. Misses (no water, no hit) write alpha 0 so the composite pass
// shows the full-resolution sky underneath.
fragment float4 march_fs(
    MarchVOut in                          [[stage_in]],
    constant MarchUniforms& U             [[buffer(0)]],
    texture3d<uint, access::read> world   [[texture(0)]],
    texturecube<float> sky_cube           [[texture(2)]])
{
    constexpr sampler cube_smp(filter::linear, address::clamp_to_edge);

    float4 p0 = U.inv_view_proj * float4(in.ndc, 0.0, 1.0);
    float4 p1 = U.inv_view_proj * float4(in.ndc, 1.0, 1.0);
    float3 near_pt = p0.xyz / p0.w;
    float3 dir = normalize(p1.xyz / p1.w - near_pt);
    // Perspective (ortho_backup == 0): keep the eye origin exactly. Orthographic:
    // parallel rays need per-pixel origins, backed up beyond the grid AABB.
    float3 org = U.ortho_backup > 0.0 ? near_pt - dir * U.ortho_backup : U.camera_pos;

    VoxelGridDesc vg = {U.grid_extent, U.height_cells, U.voxel_size_m, U.height_step_m, U.base_depth_m};
    float half_patch = vg_half_patch(vg);
    GridDims G;
    G.bmin = float3(-half_patch, -U.base_depth_m, -half_patch);
    G.bmax = float3( half_patch, vg_world_top_y(vg), half_patch);
    G.cell = float3(U.voxel_size_m, U.height_step_m, U.voxel_size_m);
    G.dims = int3(U.grid_extent, U.height_cells, U.grid_extent);

    DdaState S;
    if (!dda_init(S, org, dir, G)) return float4(0.0);

    // Phase 1: march to the first non-air cell.
    uint mat = MAT_AIR;
    int steps = 0;
    while (steps < U.max_steps) {
        steps++;
        mat = read_material(vg, world, S.idx);
        if (mat != MAT_AIR && mat != MAT_BUBBLE) break;   // Bubble is optically Air
        if (!dda_step(S, G)) return float4(0.0);
    }
    if (mat == MAT_AIR || mat == MAT_BUBBLE) return float4(0.0);

    float3 sun = U.sun_dir;   // unit length by contract (see shader_types.h)

    // Emissive fire: full-bright, ignores sun shading and the water path so it glows.
    if (mat == MAT_FIRE) return float4(aces_tonemap(U.palette[MAT_FIRE] * 3.0), 1.0);

    if (mat != MAT_WATER) {
        // Dry terrain (island above the waterline).
        float3 color = terrain_color(mat, face_normal(S.axis, S.d), sun, U);
        return float4(aces_tonemap(color), 1.0);
    }

    // Phase 2: water interface — surface terms with the pre-bend ray.
    int    entry_axis = S.axis;
    float3 n_entry    = face_normal(entry_axis, S.d);
    float3 entry_p    = org + dir * S.t_cur;
    float3 V  = normalize(org - entry_p);
    float  nv = max(dot(n_entry, V), 0.0);
    float  F  = 0.02 + 0.98 * pow(1.0 - nv, 5.0);
    float3 R  = reflect(-V, n_entry);
    float3 reflection = sky_cube.sample(cube_smp, R).rgb
                      + U.sun_color * pow(max(dot(R, sun), 0.0), U.sun_shininess);

    float3 rdir = refract(dir, n_entry, 1.0 / U.water_ior);
    if (dot(rdir, rdir) < 1e-6) rdir = dir;   // degenerate guard
    rdir = normalize(rdir);
    DdaState W;
    bool walking = dda_init(W, entry_p + rdir * 1e-4, rdir, G);

    // Phase 3: transmit through the volume.
    float water_dist = 0.0;
    uint  end_mat = MAT_AIR;
    int   end_axis = 1;
    bool  exited_up = false;
    while (walking && steps < U.max_steps) {
        steps++;
        uint m = read_material(vg, world, W.idx);
        if (m != MAT_AIR && m != MAT_WATER && m != MAT_BUBBLE) { end_mat = m; end_axis = W.axis; break; }
        float t_enter = W.t_cur;
        bool alive = dda_step(W, G);
        if (m == MAT_WATER) water_dist += W.t_cur - t_enter;
        if (!alive) { exited_up = rdir.y > 0.0; break; }
    }

    // Opaque fish: render solid, unattenuated by the water path or surface
    // reflection, so schools read clearly through the water.
    if (end_mat == MAT_FISH)
        return float4(aces_tonemap(terrain_color(MAT_FISH, face_normal(end_axis, W.d), sun, U)), 1.0);
    if (end_mat == MAT_FIRE)
        return float4(aces_tonemap(U.palette[MAT_FIRE] * 3.0), 1.0);   // fire glows through water

    // Background behind the water along the refracted path.
    float3 bg;
    if (end_mat != MAT_AIR)  bg = terrain_color(end_mat, face_normal(end_axis, W.d), sun, U);
    else if (exited_up)      bg = sky_cube.sample(cube_smp, rdir).rgb;
    else                     bg = U.deep_water_color;   // the diorama void

    // Fog-form Beer-Lambert: absorption toward the water color, so deep
    // water keeps color body instead of going black (extinction decision).
    float3 T    = exp(-U.depth_fog_density * water_dist * U.extinction_rgb);
    float3 refr = bg * T + U.deep_water_color * (1.0 - T);

    float3 color = mix(refr, reflection, F);
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
