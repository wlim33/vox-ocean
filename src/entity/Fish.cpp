#include "entity/Fish.h"
#include "entity/Creature.h"
#include "entity/CreatureRegistry.h"
#include "world/World.h"
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
bool is_hazard(VoxMat m) {
    return m == VoxMat::Fire || m == VoxMat::Lava || m == VoxMat::Acid;
}
constexpr float kSenseRadiusM = 4.0f;
}

glm::vec2 FishSchools::steer(glm::vec2 fwd, glm::vec2 hazard, glm::vec2 food, float boldness) {
    // Provisional arbitration (finalized in Task 6): hazard repels hardest,
    // food attracts, scaled by boldness (bold fish react less). Returned vector
    // is added to the centroid heading direction by the caller.
    glm::vec2 v = hazard * (2.0f - boldness) + food * (0.5f + boldness);
    return v;
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
    for (size_t s = 0; s < centroids_.size(); ++s) {
        WanderState& c = centroids_[s];
        wander_step(c, ctx.dt, ctx.t, cfg.fish.speed_mps, half);
        glm::vec2 fwd { std::cos(c.yaw), std::sin(c.yaw) };
        glm::vec3 cpos { c.pos.x,
                         0.5f * (ctx.floor_top_y(c.pos.x, c.pos.y) + ctx.water_surface(c.pos.x, c.pos.y)),
                         c.pos.y };
        glm::vec2 hazard{0.0f, 0.0f}, food{0.0f, 0.0f};
        if (auto h = ctx.find_nearest(cpos, kSenseRadiusM, is_hazard)) {
            glm::vec2 away{ cpos.x - h->x, cpos.z - h->z };
            float len = std::sqrt(away.x*away.x + away.y*away.y);
            if (len > 1e-3f) hazard = away / len;
        }
        if (auto k = ctx.find_nearest(cpos, kSenseRadiusM,
                                      [](VoxMat m){ return m == VoxMat::Kelp; })) {
            glm::vec2 to{ k->x - cpos.x, k->z - cpos.z };
            float len = std::sqrt(to.x*to.x + to.y*to.y);
            if (len > 1e-3f) food = to / len;
        }
        float boldness = h01((uint32_t)s, 9u, (uint32_t)cfg.fish.seed ^ ((uint32_t)species_ << 16));
        glm::vec2 bias = steer(fwd, hazard, food, boldness);
        glm::vec2 nf = fwd + bias * ctx.dt * cfg.fish.speed_mps;
        float nlen = std::sqrt(nf.x*nf.x + nf.y*nf.y);
        if (nlen > 1e-3f) c.yaw = std::atan2(nf.y / nlen, nf.x / nlen);
    }
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
    pending_edits_.clear();
    const auto& p = ctx.grid.params();
    float ghalf = 0.5f * p.extent * p.voxel_size_m;
    for (const auto& f : fish_) {
        if (!f.visible) continue;
        int fix = (int)std::floor((f.pos.x + ghalf) / p.voxel_size_m);
        int fiz = (int)std::floor((f.pos.z + ghalf) / p.voxel_size_m);
        int fiy = (int)std::floor((f.pos.y + p.base_depth_m) / p.height_step_m);
        if (fix < 0 || fix >= p.extent || fiz < 0 || fiz >= p.extent
            || fiy < 0 || fiy >= p.height_cells) continue;
        uint32_t cell = (uint32_t)ctx.grid.cell_index(fix, fiy, fiz);
        if ((VoxMat)ctx.world.material()[cell] == VoxMat::Kelp)
            pending_edits_.push_back({ cell, (uint8_t)VoxMat::Water });
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
    for (const auto& e : pending_edits_)
        out.edits.push(e.first, e.second);
}
}
