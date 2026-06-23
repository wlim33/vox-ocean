#include "core/App.h"
#include "core/InputBridge.h"
#include "voxel/VoxelWorld.h"
#include "voxel/Brush.h"
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

// --- draw-by-default: active tool applied to the voxel under the cursor ---

namespace {
// Camera aimed steeply down at a 4x4 grid with a Rock floor at y=0, so the
// centre ray hits the top face (mirrors EnqueuePaintTargetsSelectedCell).
vox::App make_draw_app(vox::VoxelWorld& grid, std::vector<uint8_t>& mats) {
    vox::App app{vox::Config{}};
    app.camera().pitch_rad = 1.45f; app.camera().yaw_rad = 0.0f; app.camera().distance = 10.0f;
    mats.assign((size_t)grid.cells(), (uint8_t)vox::VoxMat::Air);
    int ext = grid.params().extent;
    for (int ix = 0; ix < ext; ++ix)
        for (int iz = 0; iz < ext; ++iz)
            mats[grid.cell_index(ix, 0, iz)] = (uint8_t)vox::VoxMat::Rock;
    return app;
}
void push_draw(vox::App& app, float x, float y) {
    vox::InputBridge bridge;
    vox::InputEvent e; e.kind = vox::InputKind::Draw; e.x = x; e.y = y; bridge.push(e);
    app.handle_input(bridge);
}
}  // namespace

TEST(App, DrawPaintAppliesActiveMaterialToHitCell) {
    vox::VoxelWorld grid({4, 4, 1.0f, 1.0f, 2.0f});
    std::vector<uint8_t> mats; vox::App app = make_draw_app(grid, mats);
    app.set_tool(vox::EditTool::Paint);
    app.set_material(vox::VoxMat::SandGrain);

    push_draw(app, 50, 50);
    app.resolve_draw(100, 100, grid, mats.data());

    ASSERT_TRUE(app.selection().has_value());
    auto edits = app.drain_pending_edits();
    ASSERT_EQ(edits.size(), 1u);
    EXPECT_EQ(edits[0].cell, app.selection()->linear_idx);
    EXPECT_EQ(edits[0].mat, (uint8_t)vox::VoxMat::SandGrain);
}

TEST(App, DrawDigSetsHitCellToAir) {
    vox::VoxelWorld grid({4, 4, 1.0f, 1.0f, 2.0f});
    std::vector<uint8_t> mats; vox::App app = make_draw_app(grid, mats);
    app.set_tool(vox::EditTool::Dig);

    push_draw(app, 50, 50);
    app.resolve_draw(100, 100, grid, mats.data());

    ASSERT_TRUE(app.selection().has_value());
    auto edits = app.drain_pending_edits();
    ASSERT_EQ(edits.size(), 1u);
    EXPECT_EQ(edits[0].cell, app.selection()->linear_idx);
    EXPECT_EQ(edits[0].mat, (uint8_t)vox::VoxMat::Air);
}

TEST(App, DrawBuildPlacesMaterialAtNeighbor) {
    vox::VoxelWorld grid({4, 4, 1.0f, 1.0f, 2.0f});
    std::vector<uint8_t> mats; vox::App app = make_draw_app(grid, mats);
    app.set_tool(vox::EditTool::Build);
    app.set_material(vox::VoxMat::Rock);

    push_draw(app, 50, 50);
    app.resolve_draw(100, 100, grid, mats.data());

    ASSERT_TRUE(app.selection().has_value());
    ASSERT_TRUE(app.selection()->has_neighbor);
    auto edits = app.drain_pending_edits();
    ASSERT_EQ(edits.size(), 1u);
    EXPECT_EQ(edits[0].cell, app.selection()->neighbor_idx);
    EXPECT_EQ(edits[0].mat, (uint8_t)vox::VoxMat::Rock);
}

TEST(App, DrawMissEnqueuesNothing) {
    vox::VoxelWorld grid({4, 4, 1.0f, 1.0f, 2.0f});
    std::vector<uint8_t> mats((size_t)grid.cells(), (uint8_t)vox::VoxMat::Air);  // empty: ray hits nothing
    vox::App app{vox::Config{}};
    app.camera().pitch_rad = 1.45f; app.camera().yaw_rad = 0.0f; app.camera().distance = 10.0f;
    app.set_tool(vox::EditTool::Paint);

    push_draw(app, 50, 50);
    app.resolve_draw(100, 100, grid, mats.data());

    EXPECT_FALSE(app.selection().has_value());
    EXPECT_TRUE(app.drain_pending_edits().empty());
}

TEST(App, DrawDefaultsArePaintRock) {
    vox::App app{vox::Config{}};
    EXPECT_EQ(app.tool(), vox::EditTool::Paint);
    EXPECT_EQ(app.material(), vox::VoxMat::Rock);
    EXPECT_FALSE(app.has_pending_draw());
}

TEST(App, BrushRadiusClampsToRange) {
    vox::App app{vox::Config{}};
    EXPECT_EQ(app.brush_radius(), 0);                       // default single voxel
    app.set_brush_radius(100); EXPECT_EQ(app.brush_radius(), vox::App::kMaxBrushRadius);
    app.set_brush_radius(-5);  EXPECT_EQ(app.brush_radius(), 0);
    app.set_brush_radius(3);   EXPECT_EQ(app.brush_radius(), 3);
}

TEST(App, DrawPaintRadiusFillsSphereOfActiveMaterial) {
    vox::VoxelWorld grid({8, 8, 1.0f, 1.0f, 2.0f});
    std::vector<uint8_t> mats; vox::App app = make_draw_app(grid, mats);
    app.set_tool(vox::EditTool::Paint);
    app.set_material(vox::VoxMat::SandGrain);
    app.set_brush_radius(1);

    push_draw(app, 50, 50);
    app.resolve_draw(100, 100, grid, mats.data());
    ASSERT_TRUE(app.selection().has_value());

    auto edits = app.drain_pending_edits();
    std::vector<uint32_t> expected;
    vox::sphere_cells(grid, app.selection()->linear_idx, 1, expected);
    EXPECT_EQ(edits.size(), expected.size());               // one edit per sphere cell
    EXPECT_GT(edits.size(), 1u);                            // genuinely a volume
    for (const auto& e : edits) EXPECT_EQ(e.mat, (uint8_t)vox::VoxMat::SandGrain);
    // the centre (hit) cell is in the edit set
    bool has_center = false;
    for (const auto& e : edits) if (e.cell == app.selection()->linear_idx) has_center = true;
    EXPECT_TRUE(has_center);
}

TEST(App, DrawBuildRadiusFillsSphereAtNeighbor) {
    vox::VoxelWorld grid({8, 8, 1.0f, 1.0f, 2.0f});
    std::vector<uint8_t> mats; vox::App app = make_draw_app(grid, mats);
    app.set_tool(vox::EditTool::Build);
    app.set_material(vox::VoxMat::Rock);
    app.set_brush_radius(1);

    push_draw(app, 50, 50);
    app.resolve_draw(100, 100, grid, mats.data());
    ASSERT_TRUE(app.selection().has_value());
    ASSERT_TRUE(app.selection()->has_neighbor);

    auto edits = app.drain_pending_edits();
    std::vector<uint32_t> expected;
    vox::sphere_cells(grid, app.selection()->neighbor_idx, 1, expected);
    EXPECT_EQ(edits.size(), expected.size());               // ball centred on the neighbour
    for (const auto& e : edits) EXPECT_EQ(e.mat, (uint8_t)vox::VoxMat::Rock);
}
