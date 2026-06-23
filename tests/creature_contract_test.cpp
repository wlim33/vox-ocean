// tests/creature_contract_test.cpp
#include "entity/Ecosystem.h"
#include "entity/Creature.h"
#include "world/World.h"
#include "core/Config.h"
#include <gtest/gtest.h>
#include <memory>

using namespace vox;

// Minimal creature: records that each phase ran, publishes one body, and on act
// emits one durable edit + one occupancy cell.
struct FakeCreature : ICreature {
    mutable int published = 0, acted = 0;
    int updated = 0;
    void rebuild(const Config&, const World&) override {}
    uint16_t species_id() const override { return Species_Minnow; }
    void publish_presence(CreatureRegistry& reg) const override {
        reg.add(CreaturePresence{ {0,0,0}, 0.0f, Species_Minnow, 0 });
        ++published;
    }
    void update(const CreatureCtx& ctx) override {
        // Proves we can see our own published body via the registry.
        ctx.for_each_neighbor({0,0,0}, 1.0f, [&](const CreaturePresence&){ ++updated; });
    }
    void act(const VoxelWorld&, CreatureActs& out) const override {
        out.occupancy.push(0u, VoxMat::Fish);
        out.edits.push(7u, (uint8_t)VoxMat::Water);
        ++acted;
    }
};

static Config eco_cfg() {
    Config c;
    c.voxel.grid_extent = 32; c.voxel.height_cells = 64;
    c.voxel.voxel_size_m = 0.5f; c.voxel.height_step_m = 0.25f; c.voxel.base_depth_m = 10.0f;
    c.kelp.enabled = false; c.fish.enabled = false; c.entity.boat_enabled = false;
    return c;
}

TEST(CreatureContract, EcosystemDrivesPublishUpdateAndCollectsEdits) {
    Config c = eco_cfg();
    World world; world.configure(c);
    Ecosystem eco;
    eco.rebuild_if_dirty(c, world);
    auto fake = std::make_unique<FakeCreature>();
    FakeCreature* raw = fake.get();
    eco.add_creature(std::move(fake));

    eco.update(c, 1.0f/60.0f, 0.0f, [](float,float){ return 0.0f; }, world);
    EXPECT_EQ(raw->published, 1);
    EXPECT_EQ(raw->updated, 1);                 // saw its own presence this frame

    StampList occ;
    eco.build_stamp(c, world.grid(), occ);
    EXPECT_EQ(raw->acted, 1);
    EXPECT_EQ(occ.count(), 1);                  // occupancy routed
    ASSERT_EQ(eco.edits().idx.size(), 1u);      // durable edit routed
    EXPECT_EQ(eco.edits().idx[0], 7u);
    EXPECT_EQ(eco.edits().mat[0], (uint8_t)VoxMat::Water);
}
