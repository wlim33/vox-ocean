#include "voxel/Ripple.h"
#include <gtest/gtest.h>
#include <cmath>
#include <vector>

static constexpr int N = 32;
static std::vector<float> zeros() { return std::vector<float>(N * N, 0.0f); }
static int at(int x, int z) { return z * N + x; }

TEST(Ripple, KIsCflClamped) {
    // k = (c*dt/dx)^2 capped at 0.45 (2D stability bound is 0.5).
    EXPECT_NEAR(vox::ripple_k(6.0f, 1.0f / 60.0f, 0.5f), 0.04f, 1e-4f);
    EXPECT_FLOAT_EQ(vox::ripple_k(20.0f, 1.0f / 60.0f, 0.05f), 0.45f);  // would be 44.4
}

TEST(Ripple, FlatStaysFlat) {
    auto prev = zeros(), cur = zeros(), next = zeros();
    vox::ripple_step(prev, cur, next, N, 0.04f, 0.995f);
    for (float v : next) EXPECT_FLOAT_EQ(v, 0.0f);
}

TEST(Ripple, ImpulsePropagatesSymmetrically) {
    auto prev = zeros(), cur = zeros(), next = zeros();
    cur[at(16, 16)] = 1.0f;
    // 8 steps: ring expands; the four axis neighbors at equal distance must
    // carry identical height (any asymmetry = an indexing bug).
    for (int s = 0; s < 8; ++s) {
        vox::ripple_step(prev, cur, next, N, 0.04f, 1.0f);
        std::swap(prev, cur); std::swap(cur, next);
    }
    float xm = cur[at(12, 16)], xp = cur[at(20, 16)];
    float zm = cur[at(16, 12)], zp = cur[at(16, 20)];
    EXPECT_FLOAT_EQ(xm, xp);
    EXPECT_FLOAT_EQ(xm, zm);
    EXPECT_FLOAT_EQ(xm, zp);
    EXPECT_NE(cur[at(16, 16)], 1.0f);     // the impulse moved
}

TEST(Ripple, DampingDecaysEnergy) {
    auto prev = zeros(), cur = zeros(), next = zeros();
    cur[at(16, 16)] = 1.0f;
    auto energy = [&](const std::vector<float>& h) {
        double e = 0; for (float v : h) e += std::abs(v); return e;
    };
    double e0 = -1.0;
    for (int s = 0; s < 200; ++s) {
        vox::ripple_step(prev, cur, next, N, 0.04f, 0.97f);
        std::swap(prev, cur); std::swap(cur, next);
        if (s == 20) e0 = energy(cur);
    }
    EXPECT_LT(energy(cur), e0 * 0.5);     // strongly damped after 180 more steps
}

TEST(Ripple, StableAtDefaultsLongRun) {
    // Impulse near the absorbing border, 500 default-knob steps: bounded.
    auto prev = zeros(), cur = zeros(), next = zeros();
    cur[at(2, 2)] = 1.0f;
    for (int s = 0; s < 500; ++s) {
        vox::ripple_step(prev, cur, next, N, 0.04f, 0.995f);
        std::swap(prev, cur); std::swap(cur, next);
    }
    for (float v : cur) {
        EXPECT_TRUE(std::isfinite(v));
        EXPECT_LE(std::abs(v), 1.0f);
    }
}
