#include "voxel/Dda.h"
#include <algorithm>
#include <cmath>
namespace vox {

DdaHit dda_march(glm::vec3 origin, glm::vec3 dir, const VoxelWorld& w,
                 const uint8_t* mats, int max_steps) {
    const auto& p = w.params();
    glm::vec3 bmin(-0.5f * w.patch_size_m(), -p.base_depth_m, -0.5f * w.patch_size_m());
    glm::vec3 bmax( 0.5f * w.patch_size_m(),  w.world_top_y(),  0.5f * w.patch_size_m());
    glm::vec3 cell(p.voxel_size_m, p.height_step_m, p.voxel_size_m);
    glm::ivec3 dims(p.extent, p.height_cells, p.extent);

    DdaHit r;
    // Slab test; epsilon-pinned direction components instead of IEEE inf so
    // the shader mirror behaves identically on all GPUs.
    float tmin = 0.0f, tmax = 1e30f;
    int enter_axis = -1;
    glm::vec3 d;
    for (int a = 0; a < 3; ++a) {
        d[a] = std::abs(dir[a]) > 1e-8f ? dir[a] : (dir[a] >= 0.0f ? 1e-8f : -1e-8f);
        float t0 = (bmin[a] - origin[a]) / d[a];
        float t1 = (bmax[a] - origin[a]) / d[a];
        if (t0 > t1) std::swap(t0, t1);
        if (t0 > tmin) { tmin = t0; enter_axis = a; }
        tmax = std::min(tmax, t1);
    }
    if (tmin > tmax) return r;   // misses the diorama

    glm::vec3 pos = origin + dir * (tmin + 1e-4f);
    glm::ivec3 idx;
    for (int a = 0; a < 3; ++a)
        idx[a] = std::clamp((int)std::floor((pos[a] - bmin[a]) / cell[a]), 0, dims[a] - 1);

    glm::ivec3 step;
    glm::vec3 t_max, t_delta;
    for (int a = 0; a < 3; ++a) {
        step[a]    = d[a] > 0.0f ? 1 : -1;
        float next = bmin[a] + (float)(idx[a] + (step[a] > 0 ? 1 : 0)) * cell[a];
        t_max[a]   = (next - origin[a]) / d[a];
        t_delta[a] = cell[a] / std::abs(d[a]);
    }

    int   axis  = enter_axis < 0 ? 1 : enter_axis;  // started inside: call it a y face
    float t_cur = tmin;
    for (int s = 0; s < max_steps; ++s) {
        r.steps = s + 1;
        if (mats[w.cell_index(idx.x, idx.y, idx.z)] != (uint8_t)VoxMat::Air) {
            r.hit = true;
            r.ix = idx.x; r.iy = idx.y; r.iz = idx.z;
            r.face_axis = axis; r.t = t_cur;
            return r;
        }
        axis = (t_max.x < t_max.y) ? (t_max.x < t_max.z ? 0 : 2)
                                   : (t_max.y < t_max.z ? 1 : 2);
        t_cur = t_max[axis];
        idx[axis] += step[axis];
        if (idx[axis] < 0 || idx[axis] >= dims[axis]) return r;   // exited
        t_max[axis] += t_delta[axis];
    }
    return r;
}
}
