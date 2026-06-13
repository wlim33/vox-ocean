#include "entity/Fish.h"
#include "core/Config.h"
#include <algorithm>
#include <cmath>
namespace vox {
namespace {
uint32_t hashu(uint32_t a, uint32_t b, uint32_t seed) {
    uint32_t h = a * 0x8da6b343u + b * 0xd8163841u + seed * 0xcb1ab31fu;
    h ^= h >> 13; h *= 0x7feb352du; h ^= h >> 15;
    return h;
}
float h01(uint32_t a, uint32_t b, uint32_t seed) {
    return (float)(hashu(a, b, seed) & 0xffffffu) / 16777216.0f;
}
constexpr float FISH_BOB_AMP  = 0.2f;   // metres, half-amplitude of the vertical bob
constexpr float FISH_BOB_FREQ = 1.7f;   // rad/s
}

void FishSchools::rebuild(const Config& cfg) {
    centroids_.clear(); offset_.clear(); school_of_.clear(); bob_phase_.clear(); fish_.clear();
    if (!cfg.fish.enabled) return;
    int S = cfg.fish.school_count, per = cfg.fish.per_school;
    uint32_t seed = (uint32_t)cfg.fish.seed;
    float half = 0.5f * cfg.voxel.grid_extent * cfg.voxel.voxel_size_m;
    for (int s = 0; s < S; ++s) {
        WanderState w;
        w.pos = { (h01((uint32_t)s, 0u, seed) * 2.0f - 1.0f) * 0.5f * half,
                  (h01((uint32_t)s, 1u, seed) * 2.0f - 1.0f) * 0.5f * half };
        w.yaw = h01((uint32_t)s, 2u, seed) * 6.2831853f;
        centroids_.push_back(w);
        for (int m = 0; m < per; ++m) {
            uint32_t fk = (uint32_t)(s * 1009 + m);
            offset_.push_back({ (h01(fk, 3u, seed) * 2.0f - 1.0f) * cfg.fish.spread_m,
                                (h01(fk, 4u, seed) * 2.0f - 1.0f) * cfg.fish.spread_m });
            school_of_.push_back(s);
            bob_phase_.push_back(h01(fk, 5u, seed) * 6.2831853f);
        }
    }
    fish_.resize(offset_.size());
}

void FishSchools::update(const Config& cfg, float dt, float t,
                         const HeightFn& surface, const FloorFn& floor_fn) {
    if (fish_.empty()) return;
    float half = 0.5f * cfg.voxel.grid_extent * cfg.voxel.voxel_size_m;
    for (auto& c : centroids_)
        wander_step(c, dt, t, cfg.fish.speed_mps, half);
    float margin = 2.0f * cfg.voxel.height_step_m;
    for (size_t i = 0; i < fish_.size(); ++i) {
        const WanderState& c = centroids_[school_of_[i]];
        glm::vec2 fwd { std::cos(c.yaw), std::sin(c.yaw) };
        glm::vec2 right { -fwd.y, fwd.x };
        glm::vec2 xz = c.pos + fwd * offset_[i].x + right * offset_[i].y;
        float lo = floor_fn(xz.x, xz.y) + margin;
        float hi = surface(xz.x, xz.y) - margin;
        if (hi < lo) hi = lo;
        float band = lo + (hi - lo) * cfg.fish.depth_frac;
        float bob = FISH_BOB_AMP * std::sin(t * FISH_BOB_FREQ + bob_phase_[i]);
        fish_[i].pos = { xz.x, std::clamp(band + bob, lo, hi), xz.y };
        fish_[i].yaw = c.yaw;
    }
}

void FishSchools::build_stamp(const Config& cfg, const VoxelWorld& w, StampList& out) const {
    if (!cfg.fish.enabled) return;
    const auto& p = w.params();
    float half = 0.5f * p.extent * p.voxel_size_m;
    for (const auto& f : fish_) {
        glm::vec2 fwd { std::cos(f.yaw), std::sin(f.yaw) };
        for (int s = 0; s < FISH_CELLS; ++s) {
            float along = -(float)s * p.voxel_size_m;   // body+tail trails behind the head
            float wx = f.pos.x + fwd.x * along;
            float wz = f.pos.z + fwd.y * along;
            int ix = std::clamp((int)std::floor((wx + half) / p.voxel_size_m), 0, p.extent - 1);
            int iz = std::clamp((int)std::floor((wz + half) / p.voxel_size_m), 0, p.extent - 1);
            int iy = std::clamp((int)std::floor((f.pos.y + p.base_depth_m) / p.height_step_m),
                                0, p.height_cells - 1);
            out.push((uint32_t)w.cell_index(ix, iy, iz), VoxMat::Fish);
        }
    }
}
}
