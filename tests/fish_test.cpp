// tests/fish_test.cpp
#include "entity/Fish.h"
#include "entity/Creature.h"
#include "entity/CreatureRegistry.h"
#include "entity/StampBudget.h"
#include "world/World.h"
#include "voxel/VoxelWorld.h"
#include "core/Config.h"
#include <gtest/gtest.h>
#include <cmath>

using namespace vox;

static Config fish_cfg() {
    Config c;
    c.voxel.grid_extent = 64; c.voxel.height_cells = 64;
    c.voxel.voxel_size_m = 0.5f; c.voxel.height_step_m = 0.25f; c.voxel.base_depth_m = 10.0f;
    c.kelp.enabled = false; c.entity.boat_enabled = false;
    c.fish.enabled = true; c.fish.school_count = 3; c.fish.per_school = 10;
    c.fish.speed_mps = 2.0f; c.fish.depth_frac = 0.5f; c.fish.spread_m = 3.0f; c.fish.seed = 202;
    return c;
}

// Surface fixed at y=0 for predictable banding (engine uses kSeaLevelY likewise).
static float surf0(float, float) { return 0.0f; }

static CreatureCtx make_ctx(const Config& c, const World& w, CreatureRegistry& reg,
                            float dt, float t) {
    return CreatureCtx{ c, dt, t, w, w.grid(), surf0, reg };
}

TEST(Fish, RebuildIsDeterministicAndCounts) {
    Config c = fish_cfg();
    World w; w.configure(c);
    FishSchools a, b;
    a.rebuild(c, w); b.rebuild(c, w);
    EXPECT_EQ(a.fish().size(), (size_t)(c.fish.school_count * c.fish.per_school));
    EXPECT_EQ(a.fish().size(), b.fish().size());
}

TEST(Fish, UpdateIsDeterministic) {
    Config c = fish_cfg();
    World w; w.configure(c);
    FishSchools a, b; a.rebuild(c, w); b.rebuild(c, w);
    CreatureRegistry ra, rb;
    for (int i = 0; i < 200; ++i) {
        float t = i / 60.0f;
        auto ca = make_ctx(c, w, ra, 1.0f/60.0f, t);
        auto cb = make_ctx(c, w, rb, 1.0f/60.0f, t);
        a.update(ca); b.update(cb);
    }
    ASSERT_EQ(a.fish().size(), b.fish().size());
    for (size_t i = 0; i < a.fish().size(); ++i) {
        EXPECT_FLOAT_EQ(a.fish()[i].pos.x, b.fish()[i].pos.x);
        EXPECT_FLOAT_EQ(a.fish()[i].pos.y, b.fish()[i].pos.y);
        EXPECT_FLOAT_EQ(a.fish()[i].pos.z, b.fish()[i].pos.z);
    }
}

TEST(Fish, HoldsTheMidwaterDepthBand) {
    Config c = fish_cfg();
    World w; w.configure(c);
    FishSchools sch; sch.rebuild(c, w);
    CreatureRegistry reg;
    for (int i = 0; i < 200; ++i) {
        auto ctx = make_ctx(c, w, reg, 1.0f/60.0f, i/60.0f);
        sch.update(ctx);
    }
    for (const auto& f : sch.fish()) {
        if (!f.visible) continue;
        float lo = w.floor_top_y(f.pos.x, f.pos.z);
        EXPECT_GT(f.pos.y, lo);          // above its local floor
        EXPECT_LT(f.pos.y, 0.0f);        // below the surface
    }
}

TEST(Fish, ActMarksFishVoxels) {
    Config c = fish_cfg();
    World w; w.configure(c);
    FishSchools sch; sch.rebuild(c, w);
    CreatureRegistry reg;
    auto ctx = make_ctx(c, w, reg, 1.0f/60.0f, 0.0f);
    sch.update(ctx);
    StampList occ; EditList ed; CreatureActs acts{occ, ed};
    sch.act(w.grid(), acts);
    int visible = 0;
    for (const auto& f : sch.fish()) if (f.visible) ++visible;
    EXPECT_EQ(occ.count(), visible * FISH_CELLS);
    for (int i = 0; i < occ.count(); ++i)
        EXPECT_EQ((int)occ.mat[i], (int)VoxMat::Fish);
}

TEST(Fish, PublishesOnePresencePerVisibleFish) {
    Config c = fish_cfg();
    World w; w.configure(c);
    FishSchools sch; sch.rebuild(c, w);
    CreatureRegistry reg;
    auto ctx = make_ctx(c, w, reg, 1.0f/60.0f, 0.0f);
    sch.update(ctx);
    CreatureRegistry pub;
    sch.publish_presence(pub);
    int visible = 0;
    for (const auto& f : sch.fish()) if (f.visible) ++visible;
    EXPECT_EQ(pub.size(), visible);
}

// A school centroid placed next to Fire should, after a tick, have its members'
// average position move away from the hazard relative to a hazard-free run.
TEST(Fish, FleesNearbyHazard) {
    Config c = fish_cfg();
    c.fish.school_count = 1; c.fish.per_school = 1; c.fish.speed_mps = 2.0f;
    World hazard; hazard.configure(c);
    World calm;   calm.configure(c);

    // Seed a Fire cell near grid center in the hazard world.
    const VoxelWorld& g = hazard.grid();
    int ix = g.params().extent / 2, iy = g.params().height_cells / 2, iz = ix;
    hazard.apply_user_edit((uint32_t)g.cell_index(ix, iy, iz), (uint8_t)VoxMat::Fire);

    FishSchools fh; fh.rebuild(c, hazard);
    FishSchools fc; fc.rebuild(c, calm);
    CreatureRegistry rh, rc;
    for (int i = 0; i < 30; ++i) {
        auto ch = make_ctx(c, hazard, rh, 1.0f/60.0f, i/60.0f);
        auto cc = make_ctx(c, calm,   rc, 1.0f/60.0f, i/60.0f);
        fh.update(ch); fc.update(cc);
    }
    // Hazard-fleeing fish ends farther from the Fire cell center than the calm one.
    glm::vec3 hazpos{ g.column_center_x(ix), g.cell_bottom_y(iy), g.column_center_z(iz) };
    auto d2 = [&](const glm::vec3& p){ float dx=p.x-hazpos.x, dz=p.z-hazpos.z; return dx*dx+dz*dz; };
    EXPECT_GT(d2(fh.fish()[0].pos), d2(fc.fish()[0].pos));
}

// A fish whose body cell overlaps Kelp emits a durable edit clearing that cell to Water.
TEST(Fish, EatsKelpOnContactEmitsDurableEdit) {
    Config c = fish_cfg();
    c.fish.school_count = 1; c.fish.per_school = 1;
    World w; w.configure(c);
    FishSchools fh; fh.rebuild(c, w);
    CreatureRegistry reg;
    auto ctx = make_ctx(c, w, reg, 1.0f/60.0f, 0.0f);
    fh.update(ctx);
    // Plant Kelp at the fish's current cell so the next update senses contact.
    const VoxelWorld& g = w.grid();
    const Fish& f = fh.fish()[0];
    int ix, iy, iz; // reuse the same world->cell mapping the ctx uses
    {
        float half = 0.5f * g.params().extent * g.params().voxel_size_m;
        ix = (int)std::floor((f.pos.x + half) / g.params().voxel_size_m);
        iz = (int)std::floor((f.pos.z + half) / g.params().voxel_size_m);
        iy = (int)std::floor((f.pos.y + g.params().base_depth_m) / g.params().height_step_m);
    }
    w.apply_user_edit((uint32_t)g.cell_index(ix, iy, iz), (uint8_t)VoxMat::Kelp);
    auto ctx2 = make_ctx(c, w, reg, 1.0f/60.0f, 1.0f/60.0f);
    fh.update(ctx2);
    StampList occ; EditList ed; CreatureActs acts{occ, ed};
    fh.act(g, acts);
    ASSERT_GE(ed.idx.size(), 1u);
    bool cleared = false;
    for (size_t k = 0; k < ed.idx.size(); ++k)
        if (ed.mat[k] == (uint8_t)VoxMat::Water) cleared = true;
    EXPECT_TRUE(cleared);
}
