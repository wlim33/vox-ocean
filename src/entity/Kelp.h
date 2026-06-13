#pragma once
#include "voxel/VoxelWorld.h"
#include "voxel/FloorGen.h"
#include "entity/StampBudget.h"
#include <cstdint>
#include <functional>
#include <vector>
#include <glm/glm.hpp>
namespace vox {
struct Config;

// One rooted kelp stalk. Placement (ix,iz,base_cell,height,phase) is static —
// computed once on rebuild from the seed + floor. `lean` is refreshed each
// frame from the live water-height gradient (the boat's wake is already summed
// into that field), so the stalk sways and reacts to the boat.
struct KelpStalk {
    int   ix, iz;
    int   base_cell;       // floor top cell (first cell above terrain)
    int   height_cells;    // vertical grid layers this stalk spans
    float phase;           // per-stalk sway phase
    glm::vec2 lean {0.0f, 0.0f};   // raw water-height gradient at the anchor
};

class KelpBed {
public:
    using HeightFn = std::function<float(float, float)>;   // world (x,z) -> water y
    // Static placement: exactly kelp_stalk_count(cfg) stalks (minus any with no
    // head-room), scattered by a seeded hash, rooted at the supplied floor top.
    void rebuild(const Config& cfg, const std::vector<FloorColumn>& floor);
    // Refresh each stalk's lean from the ±2-cell water-height gradient; store t
    // for the traveling-wave sway. Deterministic.
    void update(const Config& cfg, float t, const HeightFn& water_height);
    // Rasterize the swaying stalks into the stamp list (VoxMat::Kelp).
    void build_stamp(const Config& cfg, const VoxelWorld& w, StampList& out) const;
    const std::vector<KelpStalk>& stalks() const { return stalks_; }
private:
    std::vector<KelpStalk> stalks_;
    float time_ = 0.0f;
};
}
