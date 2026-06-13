#include "entity/Wander.h"
#include "entity/Boat.h"
#include <gtest/gtest.h>
#include <cmath>

static float flatw(float, float) { return 2.0f; }

TEST(Wander, IsDeterministic) {
    vox::WanderState a, b;
    for (int i = 0; i < 300; ++i) {
        float t = i / 60.0f;
        vox::wander_step(a, 1.0f/60.0f, t, 1.5f, 48.0f);
        vox::wander_step(b, 1.0f/60.0f, t, 1.5f, 48.0f);
    }
    EXPECT_FLOAT_EQ(a.pos.x, b.pos.x);
    EXPECT_FLOAT_EQ(a.pos.y, b.pos.y);
    EXPECT_FLOAT_EQ(a.yaw,   b.yaw);
}

TEST(Wander, StaysInsideThePatch) {
    vox::WanderState s;
    float max_r = 0.0f;
    for (int i = 0; i < 6000; ++i) {
        vox::wander_step(s, 1.0f/60.0f, i/60.0f, 2.0f, 20.0f);
        max_r = std::max(max_r, std::hypot(s.pos.x, s.pos.y));
    }
    EXPECT_LT(max_r, 19.0f);
}

TEST(Wander, MatchesBoatTrajectory) {
    // The boat is wander + heave; on flat water its xz/yaw must agree with the
    // extracted wander bit-for-bit (guards the refactor).
    vox::Boat boat;
    vox::WanderState w;
    for (int i = 0; i < 300; ++i) {
        float t = i / 60.0f;
        boat.update(1.0f/60.0f, t, flatw, 1.5f, 48.0f, 0.5f);
        vox::wander_step(w, 1.0f/60.0f, t, 1.5f, 48.0f);
    }
    EXPECT_FLOAT_EQ(boat.state().pos.x, w.pos.x);
    EXPECT_FLOAT_EQ(boat.state().pos.y, w.pos.y);
    EXPECT_FLOAT_EQ(boat.state().yaw,   w.yaw);
}
