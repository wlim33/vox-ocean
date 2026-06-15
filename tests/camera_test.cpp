#include <gtest/gtest.h>
#include "core/OrbitCamera.h"
#include <glm/gtc/matrix_access.hpp>

TEST(OrbitCamera, ViewTransformsTargetToOrigin) {
    vox::OrbitCamera c;
    c.set_target({10, 0, -5});
    glm::vec4 tv = c.view() * glm::vec4(10, 0, -5, 1);
    EXPECT_NEAR(tv.x, 0.0f, 1e-3f);
    EXPECT_NEAR(tv.y, 0.0f, 1e-3f);
}

TEST(OrbitCamera, ZoomDecreasesDistance) {
    vox::OrbitCamera c;
    float d0 = c.distance;
    c.zoom(2.0f);
    EXPECT_LT(c.distance, d0);
}

TEST(OrbitCamera, PitchClampsBelowPiOverTwo) {
    vox::OrbitCamera c;
    for (int i = 0; i < 1000; ++i) c.orbit(0.0f, 50.0f);
    EXPECT_LT(c.pitch_rad, 1.5f);
    EXPECT_GT(c.pitch_rad, -1.5f);
}

TEST(OrbitCamera, CameraViewMirrorsAccessors) {
    vox::OrbitCamera c;
    c.set_target({2, 1, -3});
    vox::CameraView cv = c.camera_view();
    EXPECT_FLOAT_EQ(cv.ortho_backup, 0.0f);
    glm::vec3 p = c.position();
    EXPECT_NEAR(cv.position.x, p.x, 1e-5f);
    EXPECT_NEAR(cv.position.y, p.y, 1e-5f);
    EXPECT_NEAR(cv.position.z, p.z, 1e-5f);
    glm::mat4 vp = c.view_proj();
    EXPECT_NEAR(cv.view_proj[0][0], vp[0][0], 1e-5f);
    EXPECT_NEAR(cv.view_proj[3][3], vp[3][3], 1e-5f);
}
