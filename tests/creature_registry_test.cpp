// tests/creature_registry_test.cpp
#include "entity/CreatureRegistry.h"
#include <gtest/gtest.h>
#include <vector>

using vox::CreaturePresence;
using vox::CreatureRegistry;

static CreaturePresence at(float x, float z, uint16_t sp = 1) {
    return CreaturePresence{ {x, 0.0f, z}, 0.0f, sp, 0 };
}

TEST(CreatureRegistry, VisitsOnlyNeighborsInRadius) {
    CreatureRegistry reg;
    reg.add(at(0.0f, 0.0f));     // index 0 — at center
    reg.add(at(1.0f, 0.0f));     // index 1 — within r=2
    reg.add(at(50.0f, 0.0f));    // index 2 — far away
    std::vector<int> seen_species_counts;
    int hits = 0;
    reg.for_each({0.0f, 0.0f, 0.0f}, 2.0f,
                 [&](const CreaturePresence&) { ++hits; });
    EXPECT_EQ(hits, 2);          // center + the one at distance 1, not the far one
}

TEST(CreatureRegistry, VisitsInAscendingInsertionIndexForDeterminism) {
    CreatureRegistry reg;
    reg.add(at(0.5f, 0.0f));     // inserted first
    reg.add(at(-0.5f, 0.0f));    // inserted second (equidistant)
    std::vector<float> xs;
    reg.for_each({0.0f, 0.0f, 0.0f}, 5.0f,
                 [&](const CreaturePresence& p) { xs.push_back(p.pos.x); });
    ASSERT_EQ(xs.size(), 2u);
    EXPECT_FLOAT_EQ(xs[0], 0.5f);    // first inserted visited first
    EXPECT_FLOAT_EQ(xs[1], -0.5f);
}

TEST(CreatureRegistry, ClearEmptiesIt) {
    CreatureRegistry reg;
    reg.add(at(0.0f, 0.0f));
    reg.clear();
    int hits = 0;
    reg.for_each({0.0f, 0.0f, 0.0f}, 100.0f, [&](const CreaturePresence&){ ++hits; });
    EXPECT_EQ(hits, 0);
    EXPECT_EQ(reg.size(), 0);
}
