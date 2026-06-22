#include "core/App.h"
#include "core/InputBridge.h"
#include "voxel/VoxelWorld.h"
#include <gtest/gtest.h>
#include <vector>

TEST(App, PickResolvesAgainstGridAndStoresSelection) {
    vox::App app{vox::Config{}};            // default camera looks at origin, dist 80
    vox::VoxelWorld grid({4, 4, 1.0f, 1.0f, 2.0f});  // AABB centered on origin
    std::vector<uint8_t> mats((size_t)grid.cells(), (uint8_t)vox::VoxMat::Rock);

    vox::InputBridge bridge;
    vox::InputEvent e; e.kind = vox::InputKind::Pick; e.x = 50; e.y = 50;  // screen centre
    bridge.push(e);
    app.handle_input(bridge);
    app.resolve_pick(100, 100, grid, mats.data());

    ASSERT_TRUE(app.selection().has_value());
    EXPECT_EQ(app.selection()->material, (uint8_t)vox::VoxMat::Rock);
}

TEST(App, PickMissClearsSelection) {
    vox::App app{vox::Config{}};
    vox::VoxelWorld grid({4, 4, 1.0f, 1.0f, 2.0f});
    std::vector<uint8_t> mats((size_t)grid.cells(), (uint8_t)vox::VoxMat::Air);

    vox::InputBridge bridge;
    vox::InputEvent e; e.kind = vox::InputKind::Pick; e.x = 50; e.y = 50;
    bridge.push(e);
    app.handle_input(bridge);
    app.resolve_pick(100, 100, grid, mats.data());

    EXPECT_FALSE(app.selection().has_value());
}
