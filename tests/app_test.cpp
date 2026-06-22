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

TEST(App, EnqueueBuildWithNeighborQueuesOneEdit) {
    vox::App app{vox::Config{}};
    // Aim the camera steeply down so the centre ray hits the floor's top face
    // (face_axis y), guaranteeing an in-bounds neighbor above the hit.
    app.camera().pitch_rad = 1.45f; app.camera().yaw_rad = 0.0f; app.camera().distance = 10.0f;
    vox::VoxelWorld grid({4, 4, 1.0f, 1.0f, 2.0f});
    std::vector<uint8_t> mats((size_t)grid.cells(), (uint8_t)vox::VoxMat::Air);
    for (int ix = 0; ix < 4; ++ix)
        for (int iz = 0; iz < 4; ++iz)
            mats[grid.cell_index(ix, 0, iz)] = (uint8_t)vox::VoxMat::Rock;

    vox::InputBridge bridge;
    vox::InputEvent e; e.kind = vox::InputKind::Pick; e.x = 50; e.y = 50; bridge.push(e);
    app.handle_input(bridge);
    app.resolve_pick(100, 100, grid, mats.data());
    ASSERT_TRUE(app.selection().has_value());
    ASSERT_TRUE(app.selection()->has_neighbor);

    app.enqueue_build(vox::VoxMat::Rock);
    auto edits = app.drain_pending_edits();
    ASSERT_EQ(edits.size(), 1u);
    EXPECT_EQ(edits[0].cell, app.selection()->neighbor_idx);
    EXPECT_EQ(edits[0].mat, (uint8_t)vox::VoxMat::Rock);
    EXPECT_TRUE(app.drain_pending_edits().empty());   // queue was cleared by the drain
}

TEST(App, EnqueueBuildWithNoSelectionIsNoop) {
    vox::App app{vox::Config{}};
    app.enqueue_build(vox::VoxMat::Rock);
    EXPECT_TRUE(app.drain_pending_edits().empty());
}

TEST(App, EnqueuePaintTargetsSelectedCell) {
    vox::App app{vox::Config{}};
    app.camera().pitch_rad = 1.45f; app.camera().yaw_rad = 0.0f; app.camera().distance = 10.0f;
    vox::VoxelWorld grid({4, 4, 1.0f, 1.0f, 2.0f});
    std::vector<uint8_t> mats((size_t)grid.cells(), (uint8_t)vox::VoxMat::Air);
    for (int ix = 0; ix < 4; ++ix)
        for (int iz = 0; iz < 4; ++iz)
            mats[grid.cell_index(ix, 0, iz)] = (uint8_t)vox::VoxMat::Rock;

    vox::InputBridge bridge;
    vox::InputEvent e; e.kind = vox::InputKind::Pick; e.x = 50; e.y = 50; bridge.push(e);
    app.handle_input(bridge);
    app.resolve_pick(100, 100, grid, mats.data());
    ASSERT_TRUE(app.selection().has_value());

    app.enqueue_paint(vox::VoxMat::Rock);
    auto edits = app.drain_pending_edits();
    ASSERT_EQ(edits.size(), 1u);
    EXPECT_EQ(edits[0].cell, app.selection()->linear_idx);   // the HIT cell, not the neighbor
    EXPECT_EQ(edits[0].mat, (uint8_t)vox::VoxMat::Rock);
}

TEST(App, EnqueueDigTargetsSelectedCellWithAir) {
    vox::App app{vox::Config{}};
    app.camera().pitch_rad = 1.45f; app.camera().yaw_rad = 0.0f; app.camera().distance = 10.0f;
    vox::VoxelWorld grid({4, 4, 1.0f, 1.0f, 2.0f});
    std::vector<uint8_t> mats((size_t)grid.cells(), (uint8_t)vox::VoxMat::Air);
    for (int ix = 0; ix < 4; ++ix)
        for (int iz = 0; iz < 4; ++iz)
            mats[grid.cell_index(ix, 0, iz)] = (uint8_t)vox::VoxMat::Rock;

    vox::InputBridge bridge;
    vox::InputEvent e; e.kind = vox::InputKind::Pick; e.x = 50; e.y = 50; bridge.push(e);
    app.handle_input(bridge);
    app.resolve_pick(100, 100, grid, mats.data());
    ASSERT_TRUE(app.selection().has_value());

    app.enqueue_dig();
    auto edits = app.drain_pending_edits();
    ASSERT_EQ(edits.size(), 1u);
    EXPECT_EQ(edits[0].cell, app.selection()->linear_idx);
    EXPECT_EQ(edits[0].mat, (uint8_t)vox::VoxMat::Air);
}

TEST(App, EnqueuePaintAndDigNoSelectionAreNoops) {
    vox::App app{vox::Config{}};
    app.enqueue_paint(vox::VoxMat::Rock);
    app.enqueue_dig();
    EXPECT_TRUE(app.drain_pending_edits().empty());
}
