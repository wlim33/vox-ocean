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

// A school centroid placed next to Fire should, after many ticks, have its members'
// average position farther from the hazard relative to a hazard-free run.
TEST(Fish, FleesNearbyHazard) {
    Config c = fish_cfg();
    c.fish.school_count = 1; c.fish.per_school = 1; c.fish.speed_mps = 2.0f;

    // Both worlds share the same config (same initial fish position after rebuild).
    World hazard; hazard.configure(c);
    World calm;   calm.configure(c);

    // Rebuild and run ONE tick on each to resolve the initial fish position.
    FishSchools fh; fh.rebuild(c, hazard);
    FishSchools fc; fc.rebuild(c, calm);
    CreatureRegistry rh, rc;
    {
        auto ch = make_ctx(c, hazard, rh, 1.0f/60.0f, 0.0f);
        auto cc = make_ctx(c, calm,   rc, 1.0f/60.0f, 0.0f);
        fh.update(ch); fc.update(cc);
    }

    // Derive fire position from the fish's CURRENT xz, ~2 m offset along +x.
    // Both schools are identical so fh.fish()[0].pos == fc.fish()[0].pos here.
    const VoxelWorld& g = hazard.grid();
    const auto& gp = g.params();
    glm::vec3 fpos = fh.fish()[0].pos;
    float fire_x = fpos.x + 2.0f;   // 2 m away along +x
    float fire_z = fpos.z;
    float ghalf  = 0.5f * gp.extent * gp.voxel_size_m;
    int hix = std::clamp((int)std::floor((fire_x + ghalf) / gp.voxel_size_m), 0, gp.extent-1);
    int hiz = std::clamp((int)std::floor((fire_z + ghalf) / gp.voxel_size_m), 0, gp.extent-1);
    int hiy = std::clamp((int)std::floor((fpos.y  + gp.base_depth_m) / gp.height_step_m), 0, gp.height_cells-1);
    hazard.apply_user_edit((uint32_t)g.cell_index(hix, hiy, hiz), (uint8_t)VoxMat::Fire);

    // Record the Fire cell's world-space xz center (used as reference for both fish).
    glm::vec2 fire_xz{ g.column_center_x(hix), g.column_center_z(hiz) };

    // Run ~150 ticks so steering accumulates (start at tick 1 since we already did 0).
    for (int i = 1; i < 150; ++i) {
        auto ch = make_ctx(c, hazard, rh, 1.0f/60.0f, i/60.0f);
        auto cc = make_ctx(c, calm,   rc, 1.0f/60.0f, i/60.0f);
        fh.update(ch); fc.update(cc);
    }

    // Hazard-fleeing fish ends farther from the Fire cell than the calm one.
    auto d2xz = [&](const glm::vec3& p) {
        float dx = p.x - fire_xz.x, dz = p.z - fire_xz.y;
        return dx*dx + dz*dz;
    };
    EXPECT_GT(d2xz(fh.fish()[0].pos), d2xz(fc.fish()[0].pos));
}

// Two lone same-species fish within sensing range drift toward each other
// (cohesion) but never collapse to the same cell (separation).
TEST(Fish, SameSpeciesFlocksTowardNeighborsWithoutOverlap) {
    Config c = fish_cfg();
    c.fish.school_count = 2; c.fish.per_school = 1; c.fish.spread_m = 1.0f;
    // Use a small grid so the two centroids are close enough to fall within kSenseRadiusM=4m.
    c.voxel.grid_extent = 16;
    World w; w.configure(c);
    FishSchools fh; fh.rebuild(c, w);
    CreatureRegistry reg;
    float start = -1.0f, end = -1.0f;
    auto sep = [&](){ const auto& a = fh.fish()[0].pos; const auto& b = fh.fish()[1].pos;
                      float dx=a.x-b.x, dz=a.z-b.z; return std::sqrt(dx*dx+dz*dz); };
    {   auto ctx = make_ctx(c, w, reg, 1.0f/60.0f, 0.0f); fh.update(ctx); start = sep(); }
    for (int i = 1; i < 120; ++i) {
        reg.clear(); fh.publish_presence(reg);   // emulate Ecosystem publish pass
        auto ctx = make_ctx(c, w, reg, 1.0f/60.0f, i/60.0f);
        fh.update(ctx);
    }
    end = sep();
    EXPECT_LT(end, start);          // cohesion pulled them closer
    EXPECT_GT(end, 0.1f);           // separation kept them apart
}

// A small-species (Minnow) fish steers away from a large-species (Predator)
// presence placed nearby.
TEST(Fish, MinnowAvoidsPredatorPresence) {
    Config c = fish_cfg();
    c.fish.school_count = 1; c.fish.per_school = 1;
    World calm; calm.configure(c);
    FishSchools minnow(Species_Minnow); minnow.rebuild(c, calm);
    CreatureRegistry with_pred, without;

    // First tick to get a position.
    { auto ctx = make_ctx(c, calm, without, 1.0f/60.0f, 0.0f); minnow.update(ctx); }
    glm::vec3 start = minnow.fish()[0].pos;

    // Place a large-species presence ~2m ahead along +x.
    for (int i = 1; i < 40; ++i) {
        with_pred.clear();
        with_pred.add(CreaturePresence{ {start.x + 2.0f, start.y, start.z}, 0.0f, Species_Predator, 1 });
        auto ctx = make_ctx(c, calm, with_pred, 1.0f/60.0f, i/60.0f);
        minnow.update(ctx);
    }
    // It should end up with a smaller x than the predator (moved away from +x).
    EXPECT_LT(minnow.fish()[0].pos.x, start.x + 2.0f);
}

// Per-fish boldness is seeded at rebuild and is not uniform across individuals.
TEST(Fish, BoldnessVariesWithinSpecies) {
    Config c = fish_cfg();
    c.fish.school_count = 1; c.fish.per_school = 64;
    World w; w.configure(c);
    FishSchools fh; fh.rebuild(c, w);
    // boldness array is per-fish; assert it is not uniform.
    const std::vector<float>& b = fh.boldness_for_test();
    ASSERT_GE(b.size(), 2u);
    bool varied = false;
    for (size_t i = 1; i < b.size(); ++i) if (std::abs(b[i] - b[0]) > 1e-3f) varied = true;
    EXPECT_TRUE(varied);
}

// A visible fish whose body cell overlaps Kelp emits a durable edit clearing that cell to Water.
TEST(Fish, EatsKelpOnContactEmitsDurableEdit) {
    // Use a full multi-fish config so at least one fish is likely visible.
    Config c = fish_cfg();   // school_count=3, per_school=10 from fish_cfg()
    World w; w.configure(c);
    FishSchools fh; fh.rebuild(c, w);
    CreatureRegistry reg;
    // One update to resolve positions (and visibility).
    auto ctx0 = make_ctx(c, w, reg, 1.0f/60.0f, 0.0f);
    fh.update(ctx0);

    // Find the first visible fish — the kelp-contact guard requires visibility.
    const VoxelWorld& g = w.grid();
    const auto& gp = g.params();
    float ghalf = 0.5f * gp.extent * gp.voxel_size_m;
    int vis_idx = -1;
    for (int i = 0; i < (int)fh.fish().size(); ++i) {
        if (fh.fish()[i].visible) { vis_idx = i; break; }
    }
    bool found_visible = (vis_idx >= 0);
    ASSERT_TRUE(found_visible) << "No visible fish after one update — adjust config or seed";

    // Plant Kelp at the visible fish's current cell (same mapping Fish.cpp uses).
    const Fish& vf = fh.fish()[vis_idx];
    int ix = (int)std::floor((vf.pos.x + ghalf) / gp.voxel_size_m);
    int iz = (int)std::floor((vf.pos.z + ghalf) / gp.voxel_size_m);
    int iy = (int)std::floor((vf.pos.y + gp.base_depth_m) / gp.height_step_m);
    ix = std::clamp(ix, 0, gp.extent-1);
    iz = std::clamp(iz, 0, gp.extent-1);
    iy = std::clamp(iy, 0, gp.height_cells-1);
    w.apply_user_edit((uint32_t)g.cell_index(ix, iy, iz), (uint8_t)VoxMat::Kelp);

    // Second update: fish moves ~speed*dt = 0.033 m << 0.5 m voxel, so it stays in cell.
    auto ctx1 = make_ctx(c, w, reg, 1.0f/60.0f, 1.0f/60.0f);
    fh.update(ctx1);

    StampList occ; EditList ed; CreatureActs acts{occ, ed};
    fh.act(g, acts);
    ASSERT_GE(ed.idx.size(), 1u) << "No durable edits emitted";
    bool cleared = false;
    for (size_t k = 0; k < ed.idx.size(); ++k)
        if (ed.mat[k] == (uint8_t)VoxMat::Water) cleared = true;
    EXPECT_TRUE(cleared) << "Expected at least one edit setting VoxMat::Water";
}
