// src/entity/CreatureRegistry.cpp
#include "entity/CreatureRegistry.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
namespace vox {

int64_t CreatureRegistry::key(int bx, int bz) {
    return (int64_t)bx * 73856093LL ^ (int64_t)bz * 19349663LL;
}

void CreatureRegistry::clear() {
    items_.clear();
    buckets_.clear();
}

void CreatureRegistry::add(const CreaturePresence& p) {
    int idx = (int)items_.size();
    items_.push_back(p);
    int bx = (int)std::floor(p.pos.x / kBlockM);
    int bz = (int)std::floor(p.pos.z / kBlockM);
    buckets_[key(bx, bz)].push_back(idx);
}

void CreatureRegistry::for_each(glm::vec3 from, float radius,
                                const std::function<void(const CreaturePresence&)>& fn) const {
    int bx0 = (int)std::floor((from.x - radius) / kBlockM);
    int bx1 = (int)std::floor((from.x + radius) / kBlockM);
    int bz0 = (int)std::floor((from.z - radius) / kBlockM);
    int bz1 = (int)std::floor((from.z + radius) / kBlockM);
    // Gather candidate indices, then sort ascending so visitation is deterministic.
    std::vector<int> cand;
    for (int bz = bz0; bz <= bz1; ++bz)
        for (int bx = bx0; bx <= bx1; ++bx) {
            auto it = buckets_.find(key(bx, bz));
            if (it != buckets_.end())
                cand.insert(cand.end(), it->second.begin(), it->second.end());
        }
    std::sort(cand.begin(), cand.end());
    const float r2 = radius * radius;
    for (int i : cand) {
        const CreaturePresence& p = items_[i];
        float dx = p.pos.x - from.x, dz = p.pos.z - from.z;
        if (dx * dx + dz * dz <= r2) fn(p);
    }
}

}
