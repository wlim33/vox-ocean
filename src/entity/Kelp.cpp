#include "entity/Kelp.h"
#include "core/Config.h"
#include <algorithm>
#include <cmath>
namespace vox {
namespace {
// Deterministic integer hash -> [0,1), same mixer family as FloorGen.
uint32_t hashu(uint32_t a, uint32_t b, uint32_t seed) {
    uint32_t h = a * 0x8da6b343u + b * 0xd8163841u + seed * 0xcb1ab31fu;
    h ^= h >> 13; h *= 0x7feb352du; h ^= h >> 15;
    return h;
}
float h01(uint32_t a, uint32_t b, uint32_t seed) {
    return (float)(hashu(a, b, seed) & 0xffffffu) / 16777216.0f;
}
// Sway shape constants (internal, like the boat's wander coefficients).
constexpr float KELP_SWAY_FREQ    = 1.1f;   // rad/s of the idle oscillation
constexpr float KELP_SWAY_WAVENUM = 0.35f;  // phase advance per grid layer (traveling wave)
int world_to_col(float w, float half, float voxel, int extent) {
    return std::clamp((int)std::floor((w + half) / voxel), 0, extent - 1);
}
}

void KelpBed::rebuild(const Config& cfg, const std::vector<FloorColumn>& floor) {
    stalks_.clear();
    if (!cfg.kelp.enabled) return;
    int extent = cfg.voxel.grid_extent;
    int n = kelp_stalk_count(cfg);
    int max_layers = kelp_cells_per_stalk(cfg);
    uint32_t seed = (uint32_t)cfg.kelp.seed;
    for (int k = 0; k < n; ++k) {
        int ix = std::min(extent - 1, (int)(h01((uint32_t)k, 0u, seed) * extent));
        int iz = std::min(extent - 1, (int)(h01((uint32_t)k, 1u, seed) * extent));
        int base = floor[(size_t)iz * extent + ix].height;   // first cell above terrain
        float hf = 0.4f + 0.6f * h01((uint32_t)k, 2u, seed);
        int layers = std::max(2, (int)(hf * max_layers));
        layers = std::min(layers, cfg.voxel.height_cells - base);
        if (layers < 1) continue;                            // no head-room (tall floor)
        float phase = h01((uint32_t)k, 3u, seed) * 6.2831853f;
        stalks_.push_back({ ix, iz, base, layers, phase, {0.0f, 0.0f} });
    }
}

void KelpBed::update(const Config& cfg, float t, const HeightFn& water_height) {
    time_ = t;
    if (!cfg.kelp.enabled) return;
    float v    = cfg.voxel.voxel_size_m;
    float half = 0.5f * cfg.voxel.grid_extent * v;
    for (auto& s : stalks_) {
        float wx = ((float)s.ix + 0.5f) * v - half;   // == VoxelWorld::column_center_x(ix)
        float wz = ((float)s.iz + 0.5f) * v - half;   // == column_center_z(iz)
        float hx1 = water_height(wx + 2.0f * v, wz), hx0 = water_height(wx - 2.0f * v, wz);
        float hz1 = water_height(wx, wz + 2.0f * v), hz0 = water_height(wx, wz - 2.0f * v);
        s.lean = { hx1 - hx0, hz1 - hz0 };   // ±2-cell central difference
    }
}

void KelpBed::build_stamp(const Config& cfg, const VoxelWorld& w, StampList& out) const {
    if (!cfg.kelp.enabled) return;
    const auto& p = w.params();
    float half = 0.5f * p.extent * p.voxel_size_m;
    int cap = kelp_cells_per_stalk(cfg);
    for (const auto& s : stalks_) {
        float wx = w.column_center_x(s.ix), wz = w.column_center_z(s.iz);
        int layers = std::min(s.height_cells, cap);
        int last_cell = -1;
        for (int L = 0; L < layers; ++L) {
            float f = (float)L / (float)std::max(1, s.height_cells);   // cantilever: more bend up high
            float ax = time_ * KELP_SWAY_FREQ + s.phase + (float)L * KELP_SWAY_WAVENUM;
            glm::vec2 ambient { std::cos(ax), std::sin(ax) };
            glm::vec2 off = f * (cfg.kelp.sway_strength * s.lean
                               + cfg.kelp.sway_ambient * ambient);
            int ix = world_to_col(wx + off.x, half, p.voxel_size_m, p.extent);
            int iz = world_to_col(wz + off.y, half, p.voxel_size_m, p.extent);
            int iy = s.base_cell + L;
            if (iy < 0 || iy >= p.height_cells) continue;
            int cell = w.cell_index(ix, iy, iz);
            if (cell != last_cell) { out.push((uint32_t)cell, VoxMat::Kelp); last_cell = cell; }
        }
    }
}
}
