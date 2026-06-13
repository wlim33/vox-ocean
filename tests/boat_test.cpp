#include "entity/Boat.h"
#include "voxel/VoxelWorld.h"
#include <gtest/gtest.h>
#include <cmath>

static float flat2(float, float) { return 2.0f; }

TEST(Boat, HullHasSensibleShape) {
    int count = 0, bottom = 0, deck = 0;
    for (int x = 0; x < vox::BOAT_LEN; ++x)
        for (int y = 0; y < vox::BOAT_HGT; ++y)
            for (int z = 0; z < vox::BOAT_BEAM; ++z)
                if (vox::boat_hull(x, y, z)) {
                    count++;
                    if (y == 0) bottom++;
                    if (y == 1) deck++;
                }
    EXPECT_GT(count, 10);
    EXPECT_LT(count, vox::BOAT_LEN * vox::BOAT_HGT * vox::BOAT_BEAM);
    EXPECT_LT(bottom, deck);                 // hull narrows toward the keel
    // Beam symmetry: z and BEAM-1-z agree everywhere.
    for (int x = 0; x < vox::BOAT_LEN; ++x)
        for (int y = 0; y < vox::BOAT_HGT; ++y)
            EXPECT_EQ(vox::boat_hull(x, y, 0), vox::boat_hull(x, y, vox::BOAT_BEAM - 1));
}

TEST(Boat, DeterministicTrajectory) {
    vox::Boat a, b;
    for (int i = 0; i < 300; ++i) {
        float t = i / 60.0f;
        a.update(1.0f / 60.0f, t, flat2, 1.5f, 48.0f, 0.5f);
        b.update(1.0f / 60.0f, t, flat2, 1.5f, 48.0f, 0.5f);
    }
    EXPECT_FLOAT_EQ(a.state().pos.x, b.state().pos.x);
    EXPECT_FLOAT_EQ(a.state().pos.y, b.state().pos.y);
    EXPECT_FLOAT_EQ(a.state().yaw,   b.state().yaw);
}

TEST(Boat, HeaveSettlesOnWater) {
    vox::Boat boat;
    for (int i = 0; i < 600; ++i)
        boat.update(1.0f / 60.0f, i / 60.0f, flat2, 0.0f, 48.0f, 0.5f);
    // Flat water at y=2, draft 0.4: hull base settles at 1.6.
    EXPECT_NEAR(boat.state().y, 1.6f, 0.05f);
}

TEST(Boat, SteersAwayFromTheEdge) {
    vox::Boat boat;
    // Drive it outward for a long time on a small patch: it must stay inside.
    float max_r = 0.0f;
    for (int i = 0; i < 6000; ++i) {
        boat.update(1.0f / 60.0f, i / 60.0f, flat2, 2.0f, 20.0f, 0.5f);
        max_r = std::max(max_r, std::hypot(boat.state().pos.x, boat.state().pos.y));
    }
    EXPECT_LT(max_r, 19.0f);   // never reaches the 20m wall
}

TEST(Boat, SternIsBehindTheBow) {
    vox::Boat boat;
    boat.update(1.0f / 60.0f, 0.0f, flat2, 1.5f, 48.0f, 0.5f);
    glm::vec2 fwd { std::cos(boat.state().yaw), std::sin(boat.state().yaw) };
    glm::vec2 to_stern = boat.stern_world(0.5f) - boat.state().pos;
    EXPECT_LT(glm::dot(fwd, to_stern), 0.0f);
}

TEST(Boat, CellsCoverTheHullFootprint) {
    vox::VoxelWorld w({64, 64, 0.5f, 0.25f, 10.0f});
    vox::BoatState s;                  // pos (0,0), yaw 0, y 0 (water line)
    auto cells = vox::boat_cells(s, w);
    EXPECT_GT(cells.size(), 20u);      // hull spans multiple grid layers
    int ix, iy, iz;
    for (uint32_t c : cells) {
        w.decode_cell_index((int)c, ix, iy, iz);
        // Footprint stays within the boat's AABB around the center column.
        EXPECT_NEAR(w.column_center_x(ix), 0.0f, 0.5f * vox::BOAT_LEN * 0.5f + 0.5f);
        EXPECT_NEAR(w.column_center_z(iz), 0.0f, 0.5f * vox::BOAT_BEAM * 0.5f + 0.5f);
        EXPECT_GE(iy, 0); EXPECT_LT(iy, 64);
    }
    // Determinism.
    EXPECT_EQ(cells, vox::boat_cells(s, w));
}

TEST(Boat, WakeSheddingScalesWithDistanceNotTime) {
    // Root-cause regression: the boat must NOT shed a wake impulse every
    // frame. A near-stationary boat deposited a negative splash each frame
    // into the same ripple cells, which integrated into a self-reinforcing
    // hole the buoyancy then sank into. Wake energy scales with DISTANCE
    // traveled: ~one impulse per cell, none when dead in the water.
    glm::vec2 p;

    vox::Boat still;
    int still_count = 0;
    for (int i = 0; i < 600; ++i) {                 // 10s, speed 0
        still.update(1.0f / 60.0f, i / 60.0f, flat2, 0.0f, 48.0f, 0.5f);
        if (still.shed_wake(0.5f, p)) still_count++;
    }
    EXPECT_EQ(still_count, 0);                       // no travel -> no wake

    vox::Boat moving;
    int move_count = 0;
    for (int i = 0; i < 600; ++i) {                 // 10s at 1.5 m/s = 15m
        moving.update(1.0f / 60.0f, i / 60.0f, flat2, 1.5f, 48.0f, 0.5f);
        if (moving.shed_wake(0.5f, p)) move_count++;
    }
    EXPECT_GE(move_count, 25);                       // ~15m / 0.5m cell ~= 30
    EXPECT_LE(move_count, 35);
}
