#pragma once
#include "core/Config.h"
#include "core/OrbitCamera.h"
#include "core/Clock.h"
#include "core/InputBridge.h"
#include "voxel/Raycaster.h"
#include "voxel/VoxelWorld.h"
#include <glm/glm.hpp>
#include <optional>
#include <vector>
#include <cstdint>

namespace vox {

struct UserEdit { uint32_t cell; uint8_t mat; };

enum class EditTool : int32_t { Paint, Build, Dig };

class App {
public:
    explicit App(Config cfg);
    void handle_input(InputBridge& b);
    void update();

    // Resolve a pending Pick (from handle_input) against the world grid; updates
    // selection(). viewport is the drawable size in pixels. No-op if no pick is pending.
    void resolve_pick(int viewport_w, int viewport_h,
                      const VoxelWorld& grid, const uint8_t* materials);
    const std::optional<PickHit>& selection() const { return selection_; }
    bool has_pending_pick() const { return pending_pick_.has_value(); }

    void enqueue_build(VoxMat m);                 // queues a build into selection's neighbor
    void enqueue_paint(VoxMat m);                 // overwrites the selected cell with m
    void enqueue_dig();                           // sets the selected cell to Air
    std::vector<UserEdit> drain_pending_edits();  // returns and clears the queue

    // Resolve a pending Draw: pick under the cursor, store it as selection(), and
    // apply the active tool/material to it. No-op if no draw is pending or on a miss.
    void resolve_draw(int viewport_w, int viewport_h,
                      const VoxelWorld& grid, const uint8_t* materials);
    bool has_pending_draw() const { return pending_draw_.has_value(); }

    void     set_tool(EditTool t)     { tool_ = t; }
    void     set_material(VoxMat m)   { material_ = m; }
    EditTool tool() const             { return tool_; }
    VoxMat   material() const         { return material_; }

    static constexpr int kMaxBrushRadius = 8;
    void set_brush_radius(int r);                 // clamped to [0, kMaxBrushRadius]
    int  brush_radius() const { return brush_radius_; }

    const OrbitCamera& camera() const { return camera_; }
    OrbitCamera&       camera()       { return camera_; }
    const Config&      config() const { return config_; }
    Config&            config()       { return config_; }
    const Clock&       clock()  const { return clock_; }

    bool mouse_down = false;
    int  debug_view = 0;
private:
    Config config_;
    OrbitCamera camera_;
    Clock clock_;
    std::optional<glm::vec2> pending_pick_;   // screen px of an unresolved Pick
    std::optional<glm::vec2> pending_draw_;  // screen px of an unresolved Draw
    std::optional<PickHit>   selection_;     // last resolved hit (cleared on miss)
    std::vector<UserEdit>    pending_edits_;
    EditTool tool_     = EditTool::Paint;
    VoxMat   material_ = VoxMat::Rock;
    int brush_radius_ = 0;                         // 0 = single voxel
    std::vector<uint32_t> draw_cells_;             // resolve_draw scratch (sphere cells)
};
}
