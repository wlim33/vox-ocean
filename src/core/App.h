#pragma once
#include "core/Config.h"
#include "core/OrbitCamera.h"
#include "core/Clock.h"
#include "core/InputBridge.h"
#include "voxel/Raycaster.h"
#include "voxel/VoxelWorld.h"
#include <glm/glm.hpp>
#include <optional>

namespace vox {
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
};
}
