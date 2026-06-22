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
    std::optional<PickHit>   selection_;      // last resolved hit (cleared on miss)
    std::vector<UserEdit>    pending_edits_;
};
}
