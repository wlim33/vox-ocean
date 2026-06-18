#include "ocean/WaterModel.h"
#include "core/Config.h"
#include <gtest/gtest.h>
#include <cmath>

static vox::WaveConfig default_wave() {
    return vox::WaveConfig{};   // wind 12 m/s, dir 0.5, amplitude 4000, max_wavelength 20
}

TEST(WaterModel, DeterministicForSameConfig) {
    vox::WaterModel a, b;
    a.configure(default_wave());
    b.configure(default_wave());
    for (int i = 0; i < 50; ++i) {
        float x = (float)i * 0.7f, z = (float)i * -0.3f, t = (float)i / 60.0f;
        EXPECT_FLOAT_EQ(a.height_at(x, z, t), b.height_at(x, z, t));
    }
}

TEST(WaterModel, FlatWhenAmplitudeZero) {
    vox::WaveConfig w = default_wave();
    w.amplitude = 0.0f;
    vox::WaterModel m;
    m.configure(w);
    for (int i = 0; i < 20; ++i)
        EXPECT_NEAR(m.height_at((float)i, (float)-i, (float)i / 30.0f), 0.0f, 1e-6f);
}

TEST(WaterModel, FiniteAndBounded) {
    vox::WaterModel m;
    m.configure(default_wave());
    for (int i = 0; i < 1000; ++i) {
        float x = (float)i * 3.13f, z = (float)i * 1.77f, t = (float)i / 20.0f;
        float h = m.height_at(x, z, t);
        EXPECT_TRUE(std::isfinite(h));
        EXPECT_LT(std::fabs(h), 50.0f);   // sane meters-scale surface
    }
}

TEST(WaterModel, VariesOverTimeAndSpace) {
    vox::WaterModel m;
    m.configure(default_wave());
    EXPECT_NE(m.height_at(0.0f, 0.0f, 0.0f), m.height_at(0.0f, 0.0f, 1.0f));   // animates
    EXPECT_NE(m.height_at(0.0f, 0.0f, 0.5f), m.height_at(13.0f, 7.0f, 0.5f));  // not constant in space
}

TEST(WaterModel, ContinuousInSpace) {
    vox::WaterModel m;
    m.configure(default_wave());
    float h0 = m.height_at(5.0f, 5.0f, 2.0f);
    float h1 = m.height_at(5.01f, 5.0f, 2.0f);
    EXPECT_LT(std::fabs(h1 - h0), 0.5f);   // no discontinuity over 1cm
}
