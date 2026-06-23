// src/entity/CreatureRegistry.h
#pragma once
#include "entity/Creature.h"
#include <functional>
#include <unordered_map>
#include <vector>
namespace vox {

// Per-frame snapshot of every creature body, queried by radius. Backed by a
// coarse spatial hash so radius queries are ~O(neighbors), not O(N^2). Visits
// candidates in ascending insertion index, so flocking sums are reproducible.
class CreatureRegistry {
public:
    void clear();
    void add(const CreaturePresence& p);
    int  size() const { return (int)items_.size(); }
    void for_each(glm::vec3 from, float radius,
                  const std::function<void(const CreaturePresence&)>& fn) const;
private:
    static constexpr float kBlockM = 4.0f;   // spatial-hash block edge (metres)
    static int64_t key(int bx, int bz);
    std::vector<CreaturePresence> items_;
    std::unordered_map<int64_t, std::vector<int>> buckets_;   // block -> item indices
};

}
