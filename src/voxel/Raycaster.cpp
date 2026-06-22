#include "voxel/Raycaster.h"
#include "voxel/Dda.h"

namespace vox {

FaceNeighbor face_neighbor(const VoxelWorld& grid, int ix, int iy, int iz,
                           int face_axis, const glm::vec3& dir) {
    FaceNeighbor n;
    if (face_axis < 0) return n;
    int cell[3] = {ix, iy, iz};
    int step = dir[face_axis] > 0.0f ? 1 : -1;   // DDA step direction along the axis
    cell[face_axis] -= step;                      // the cell the ray came from
    const VoxelWorldParams& p = grid.params();
    int dims[3] = {p.extent, p.height_cells, p.extent};
    if (cell[0] < 0 || cell[0] >= dims[0] ||
        cell[1] < 0 || cell[1] >= dims[1] ||
        cell[2] < 0 || cell[2] >= dims[2]) return n;
    n.has_neighbor = true;
    n.nx = cell[0]; n.ny = cell[1]; n.nz = cell[2];
    n.idx = (uint32_t)grid.cell_index(cell[0], cell[1], cell[2]);
    return n;
}

std::optional<PickHit> pick(int viewport_w, int viewport_h,
                            float pixel_x, float pixel_y,
                            const glm::mat4& inv_view_proj,
                            const glm::vec3& camera_pos,
                            const VoxelWorld& grid,
                            const uint8_t* materials,
                            int max_steps) {
    if (viewport_w <= 0 || viewport_h <= 0) return std::nullopt;

    // Pixel (top-left origin) -> NDC. NDC y is up, so flip.
    float ndc_x = 2.0f * pixel_x / (float)viewport_w - 1.0f;
    float ndc_y = 1.0f - 2.0f * pixel_y / (float)viewport_h;

    // Unproject near/far, perspective divide, normalize — mirror of the shader.
    glm::vec4 p0 = inv_view_proj * glm::vec4(ndc_x, ndc_y, 0.0f, 1.0f);
    glm::vec4 p1 = inv_view_proj * glm::vec4(ndc_x, ndc_y, 1.0f, 1.0f);
    glm::vec3 near_pt = glm::vec3(p0) / p0.w;
    glm::vec3 far_pt  = glm::vec3(p1) / p1.w;
    glm::vec3 dir = glm::normalize(far_pt - near_pt);

    DdaHit h = dda_march(camera_pos, dir, grid, materials, max_steps);
    if (!h.hit) return std::nullopt;

    PickHit out;
    out.ix = h.ix; out.iy = h.iy; out.iz = h.iz;
    out.linear_idx = (uint32_t)grid.cell_index(h.ix, h.iy, h.iz);
    out.material = materials[out.linear_idx];
    out.face_axis = h.face_axis;
    out.t = h.t;
    FaceNeighbor n = face_neighbor(grid, h.ix, h.iy, h.iz, h.face_axis, dir);
    out.has_neighbor = n.has_neighbor;
    out.nx = n.nx; out.ny = n.ny; out.nz = n.nz;
    out.neighbor_idx = n.idx;
    return out;
}

}  // namespace vox
