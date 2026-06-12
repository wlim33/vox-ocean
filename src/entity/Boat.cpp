#include "entity/Boat.h"
#include <algorithm>
#include <cmath>
namespace vox {

bool boat_hull(int x, int y, int z) {
    if (x < 0 || x >= BOAT_LEN || y < 0 || y >= BOAT_HGT || z < 0 || z >= BOAT_BEAM)
        return false;
    bool mid = (z == BOAT_BEAM / 2);
    switch (y) {
        case 0:   // keel: center line only, stopping short of the bow tip
            return mid && x >= 1 && x <= BOAT_LEN - 2;
        case 1:   // deck: full beam amidships, pointed bow, square stern
            if (x == BOAT_LEN - 1) return mid;          // bow tip
            return true;
        case 2:   // cabin: small block aft of midship, center line
            return mid && x >= 1 && x <= 2;
        default:  return false;
    }
}

void Boat::update(float dt, float t, const HeightFn& water_height,
                  float speed_mps, float patch_half_m, float voxel_size_m) {
    // Wander: smooth deterministic steering; two incommensurate sines give a
    // non-repeating-feeling course with no RNG (bench determinism).
    float yaw_rate = 0.4f * (0.6f * std::sin(t * 0.23f) + 0.3f * std::sin(t * 0.71f));

    // Edge avoidance: beyond 60% of the half patch, blend toward the center
    // heading, fully overriding the wander at 85%.
    float r = glm::length(state_.pos);
    if (r > 0.6f * patch_half_m) {
        glm::vec2 to_center = -state_.pos / std::max(r, 1e-3f);
        float want = std::atan2(to_center.y, to_center.x);
        float diff = want - state_.yaw;
        while (diff >  3.1415927f) diff -= 6.2831853f;
        while (diff < -3.1415927f) diff += 6.2831853f;
        float urgency = std::min((r - 0.6f * patch_half_m) / (0.25f * patch_half_m), 1.0f);
        yaw_rate = glm::mix(yaw_rate, std::clamp(diff, -1.2f, 1.2f), urgency);
    }
    state_.yaw += yaw_rate * dt;
    glm::vec2 fwd { std::cos(state_.yaw), std::sin(state_.yaw) };
    state_.pos += fwd * (speed_mps * dt);

    // Heave: average water height under bow/stern/port/starboard, smoothed;
    // the hull base floats DRAFT below the surface.
    constexpr float DRAFT = 0.4f;
    glm::vec2 right { -fwd.y, fwd.x };
    float hl = 0.5f * BOAT_LEN  * voxel_size_m;
    float hb = 0.5f * BOAT_BEAM * voxel_size_m;
    float target = 0.25f * (water_height(state_.pos.x + fwd.x * hl, state_.pos.y + fwd.y * hl)
                          + water_height(state_.pos.x - fwd.x * hl, state_.pos.y - fwd.y * hl)
                          + water_height(state_.pos.x + right.x * hb, state_.pos.y + right.y * hb)
                          + water_height(state_.pos.x - right.x * hb, state_.pos.y - right.y * hb));
    if (!primed_) { heave_smooth_ = target; primed_ = true; }
    heave_smooth_ += (target - heave_smooth_) * std::min(dt * 4.0f, 1.0f);
    state_.y = heave_smooth_ - DRAFT;
}

glm::vec2 Boat::stern_world(float voxel_size_m) const {
    glm::vec2 fwd { std::cos(state_.yaw), std::sin(state_.yaw) };
    return state_.pos - fwd * (0.5f * BOAT_LEN * voxel_size_m);
}

std::vector<uint32_t> boat_cells(const BoatState& s, const VoxelWorld& w) {
    const auto& p = w.params();
    std::vector<uint32_t> out;
    glm::vec2 fwd { std::cos(s.yaw), std::sin(s.yaw) };
    glm::vec2 right { -fwd.y, fwd.x };
    float half = 0.5f * w.patch_size_m();
    float reach = 0.5f * (float)BOAT_LEN * p.voxel_size_m + p.voxel_size_m;

    int ix0 = std::max(0, (int)std::floor((s.pos.x - reach + half) / p.voxel_size_m));
    int ix1 = std::min(p.extent - 1, (int)std::floor((s.pos.x + reach + half) / p.voxel_size_m));
    int iz0 = std::max(0, (int)std::floor((s.pos.y - reach + half) / p.voxel_size_m));
    int iz1 = std::min(p.extent - 1, (int)std::floor((s.pos.y + reach + half) / p.voxel_size_m));

    // Each logical hull layer spans this many grid layers vertically.
    int vratio = std::max(1, (int)std::lround(p.voxel_size_m / p.height_step_m));
    int base_iy = (int)std::floor((s.y + p.base_depth_m) / p.height_step_m);

    for (int iz = iz0; iz <= iz1; ++iz)
        for (int ix = ix0; ix <= ix1; ++ix) {
            glm::vec2 d { w.column_center_x(ix) - s.pos.x,
                          w.column_center_z(iz) - s.pos.y };
            int lx = (int)std::floor(glm::dot(d, fwd)   / p.voxel_size_m + 0.5f * BOAT_LEN);
            int lz = (int)std::floor(glm::dot(d, right) / p.voxel_size_m + 0.5f * BOAT_BEAM);
            for (int ly = 0; ly < BOAT_HGT; ++ly) {
                if (!boat_hull(lx, ly, lz)) continue;
                for (int sub = 0; sub < vratio; ++sub) {
                    int iy = base_iy + ly * vratio + sub;
                    if (iy >= 0 && iy < p.height_cells)
                        out.push_back((uint32_t)w.cell_index(ix, iy, iz));
                }
            }
        }
    return out;
}
}
