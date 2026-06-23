#include "entity/Fish.h"
#include "entity/Creature.h"
#include "entity/CreatureRegistry.h"
#include "core/Config.h"
#include "voxel/FloorGen.h"   // kMinDepthCells
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
constexpr float FISH_BOB_AMP  = 0.2f;
constexpr float FISH_BOB_FREQ = 1.7f;
}

void FishSchools::rebuild(const Config& cfg, const World& /*world*/) {
    centroids_.clear(); offset_.clear(); school_of_.clear(); bob_phase_.clear(); fish_.clear();
    if (!cfg.fish.enabled) return;
    int S = cfg.fish.school_count, per = cfg.fish.per_school;
    uint32_t seed = (uint32_t)cfg.fish.seed ^ ((uint32_t)species_ << 16);
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

void FishSchools::update(const CreatureCtx& ctx) {
    if (fish_.empty()) return;
    const Config& cfg = ctx.cfg;
    float half = 0.5f * cfg.voxel.grid_extent * cfg.voxel.voxel_size_m;
    for (auto& c : centroids_)
        wander_step(c, ctx.dt, ctx.t, cfg.fish.speed_mps, half);
    float margin = 2.0f * cfg.voxel.height_step_m;
    for (size_t i = 0; i < fish_.size(); ++i) {
        const WanderState& c = centroids_[school_of_[i]];
        glm::vec2 fwd { std::cos(c.yaw), std::sin(c.yaw) };
        glm::vec2 right { -fwd.y, fwd.x };
        glm::vec2 xz = c.pos + fwd * offset_[i].x + right * offset_[i].y;
        float lo = ctx.floor_top_y(xz.x, xz.y) + margin;
        float hi = ctx.water_surface(xz.x, xz.y) - margin;
        if (hi < lo) hi = lo;
        float band = lo + (hi - lo) * cfg.fish.depth_frac;
        float bob = FISH_BOB_AMP * std::sin(ctx.t * FISH_BOB_FREQ + bob_phase_[i]);
        fish_[i].pos = { xz.x, std::clamp(band + bob, lo, hi), xz.y };
        fish_[i].yaw = c.yaw;
        float depth = ctx.water_surface(xz.x, xz.y) - ctx.floor_top_y(xz.x, xz.y);
        fish_[i].visible = depth >= (float)kMinDepthCells * cfg.voxel.height_step_m;
    }
}

void FishSchools::publish_presence(CreatureRegistry& reg) const {
    for (const auto& f : fish_)
        if (f.visible) reg.add(CreaturePresence{ f.pos, f.yaw, species_, 0 });
}

void FishSchools::act(const VoxelWorld& grid, CreatureActs& out) const {
    const auto& p = grid.params();
    float half = 0.5f * p.extent * p.voxel_size_m;
    for (const auto& f : fish_) {
        if (!f.visible) continue;
        glm::vec2 fwd { std::cos(f.yaw), std::sin(f.yaw) };
        int iy0 = (int)std::floor((f.pos.y + p.base_depth_m) / p.height_step_m);
        for (int s = 0; s < FISH_BODY_LEN; ++s) {
            float along = -(float)s * p.voxel_size_m;
            float wx = f.pos.x + fwd.x * along;
            float wz = f.pos.z + fwd.y * along;
            int ix = std::clamp((int)std::floor((wx + half) / p.voxel_size_m), 0, p.extent - 1);
            int iz = std::clamp((int)std::floor((wz + half) / p.voxel_size_m), 0, p.extent - 1);
            for (int dy = 0; dy < FISH_BODY_HGT; ++dy) {
                int iy = std::clamp(iy0 - FISH_BODY_HGT / 2 + dy, 0, p.height_cells - 1);
                out.occupancy.push((uint32_t)grid.cell_index(ix, iy, iz), VoxMat::Fish);
            }
        }
    }
}
}
