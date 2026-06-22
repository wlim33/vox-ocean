#include "voxel/Dda.h"
#include "voxel/GridView.h"
#include <algorithm>
#include <cmath>
namespace vox {
namespace {

// Restartable DDA core shared by dda_march and dda_march_transmit, and the
// CPU mirror of the DdaState helpers in shaders/voxel_march.metal — keep in
// lockstep. Restartability is what lets refraction re-aim the ray mid-walk.
struct DdaState {
    glm::vec3  d;            // epsilon-pinned direction
    glm::ivec3 idx, stp;
    glm::vec3  t_max, t_delta;
    float      t_cur = 0.0f; // ray t at the current cell's entry
    int        axis = 1;     // face axis the current cell was entered through
    int        enter_axis = -1;
};

struct GridDims {
    glm::vec3  bmin, bmax, cell;
    glm::ivec3 dims;
};

GridDims grid_dims(const VoxelWorld& w) {
    const auto& p = w.params();
    return { {-0.5f * w.patch_size_m(), -p.base_depth_m, -0.5f * w.patch_size_m()},
             { 0.5f * w.patch_size_m(),  w.world_top_y(),  0.5f * w.patch_size_m()},
             { p.voxel_size_m, p.height_step_m, p.voxel_size_m },
             { p.extent, p.height_cells, p.extent } };
}

// Slab test + entry-cell setup. Returns false when the ray misses the AABB.
bool dda_init(DdaState& S, glm::vec3 origin, glm::vec3 dir, const GridDims& G) {
    float tmin = 0.0f, tmax = 1e30f;
    S.enter_axis = -1;
    for (int a = 0; a < 3; ++a) {
        // Epsilon-pinned instead of IEEE inf so the shader mirror behaves
        // identically on all GPUs.
        S.d[a] = std::abs(dir[a]) > 1e-8f ? dir[a] : (dir[a] >= 0.0f ? 1e-8f : -1e-8f);
        float t0 = (G.bmin[a] - origin[a]) / S.d[a];
        float t1 = (G.bmax[a] - origin[a]) / S.d[a];
        if (t0 > t1) std::swap(t0, t1);
        if (t0 > tmin) { tmin = t0; S.enter_axis = a; }
        tmax = std::min(tmax, t1);
    }
    if (tmin > tmax) return false;

    glm::vec3 pos = origin + dir * (tmin + 1e-4f);
    for (int a = 0; a < 3; ++a) {
        S.idx[a] = std::clamp((int)std::floor((pos[a] - G.bmin[a]) / G.cell[a]),
                              0, G.dims[a] - 1);
        S.stp[a] = S.d[a] > 0.0f ? 1 : -1;
        float next = G.bmin[a] + (float)(S.idx[a] + (S.stp[a] > 0 ? 1 : 0)) * G.cell[a];
        S.t_max[a]   = (next - origin[a]) / S.d[a];
        S.t_delta[a] = G.cell[a] / std::abs(S.d[a]);
    }
    S.axis  = S.enter_axis < 0 ? 1 : S.enter_axis;  // started inside: call it a y face
    S.t_cur = tmin;
    return true;
}

// Advance one cell. Returns false when the ray leaves the grid; t_cur is
// then the boundary t where it left.
bool dda_step(DdaState& S, const GridDims& G) {
    S.axis = (S.t_max.x < S.t_max.y) ? (S.t_max.x < S.t_max.z ? 0 : 2)
                                     : (S.t_max.y < S.t_max.z ? 1 : 2);
    S.t_cur = S.t_max[S.axis];
    S.idx[S.axis] += S.stp[S.axis];
    if (S.idx[S.axis] < 0 || S.idx[S.axis] >= G.dims[S.axis]) return false;
    S.t_max[S.axis] += S.t_delta[S.axis];
    return true;
}

} // namespace

DdaHit dda_march(glm::vec3 origin, glm::vec3 dir, const VoxelWorld& w,
                 const uint8_t* mats, int max_steps) {
    GridDims G = grid_dims(w);
    auto grid = grid_view(mats, w.params().extent, w.params().height_cells);
    DdaHit r;
    DdaState S;
    if (!dda_init(S, origin, dir, G)) return r;
    for (int s = 0; s < max_steps; ++s) {
        r.steps = s + 1;
        if (grid[S.idx.x, S.idx.y, S.idx.z] != (uint8_t)VoxMat::Air) {
            r.hit = true;
            r.ix = S.idx.x; r.iy = S.idx.y; r.iz = S.idx.z;
            r.face_axis = S.axis; r.t = S.t_cur;
            return r;
        }
        if (!dda_step(S, G)) return r;
    }
    return r;
}

TransmitResult dda_march_transmit(glm::vec3 origin, glm::vec3 dir,
                                  const VoxelWorld& w, const uint8_t* mats,
                                  int max_steps, float ior) {
    GridDims G = grid_dims(w);
    auto grid = grid_view(mats, w.params().extent, w.params().height_cells);
    TransmitResult r;
    DdaState S;
    if (!dda_init(S, origin, dir, G)) return r;

    // Phase 1: march to the first non-Air cell.
    uint8_t mat = (uint8_t)VoxMat::Air;
    while (r.steps < max_steps) {
        r.steps++;
        mat = grid[S.idx.x, S.idx.y, S.idx.z];
        if (mat != (uint8_t)VoxMat::Air && mat != (uint8_t)VoxMat::Bubble) break;  // Bubble is optically Air
        if (!dda_step(S, G)) return r;     // clean miss
    }
    if (mat == (uint8_t)VoxMat::Air || mat == (uint8_t)VoxMat::Bubble) return r;   // budget exhausted in air/bubble

    if (mat != (uint8_t)VoxMat::Water) {         // dry hit (island above water)
        r.hit = true;
        r.ix = S.idx.x; r.iy = S.idx.y; r.iz = S.idx.z;
        r.opaque_axis = S.axis;
        return r;
    }

    // Phase 2: water interface — record it, bend once.
    r.entry_axis = S.axis;
    r.entry_t    = S.t_cur;
    glm::vec3 n(0.0f);
    n[S.axis] = S.d[S.axis] > 0.0f ? -1.0f : 1.0f;
    glm::vec3 entry_p = origin + dir * S.t_cur;
    glm::vec3 rdir = glm::refract(dir, n, 1.0f / ior);
    if (glm::dot(rdir, rdir) < 1e-6f) rdir = dir;   // degenerate guard
    rdir = glm::normalize(rdir);
    DdaState W;
    if (!dda_init(W, entry_p + rdir * 1e-4f, rdir, G)) return r;

    // Phase 3: transmit — accumulate metric distance inside Water cells.
    while (r.steps < max_steps) {
        r.steps++;
        uint8_t m = grid[W.idx.x, W.idx.y, W.idx.z];
        if (m != (uint8_t)VoxMat::Air && m != (uint8_t)VoxMat::Water && m != (uint8_t)VoxMat::Bubble) {
            r.hit = true;
            r.ix = W.idx.x; r.iy = W.idx.y; r.iz = W.idx.z;
            r.opaque_axis = W.axis;
            return r;
        }
        float t_enter = W.t_cur;
        bool alive = dda_step(W, G);
        if (m == (uint8_t)VoxMat::Water) r.water_dist += W.t_cur - t_enter;
        if (!alive) { r.exited_up = rdir.y > 0.0f; return r; }
    }
    return r;   // budget exhausted mid-water
}

}
