#include "core/App.h"
namespace vox {

App::App(Config cfg) : config_(std::move(cfg)) {
    camera_.distance = 80.0f;
    camera_.pitch_rad = 0.3f;
}

void App::handle_input(InputBridge& b) {
    for (auto& e : b.drain()) {
        switch (e.kind) {
            case InputKind::MouseDown: mouse_down = true; break;
            case InputKind::MouseUp:   mouse_down = false; break;
            case InputKind::MouseMove:
                if (mouse_down) camera_.orbit(e.x, e.y);
                break;
            case InputKind::Scroll:
                camera_.zoom(e.scroll);
                break;
            case InputKind::Resize:
                if (e.height > 0) camera_.set_aspect((float)e.width / (float)e.height);
                break;
            case InputKind::Pick:
                pending_pick_ = glm::vec2{e.x, e.y};
                break;
            default: break;
        }
    }
}

void App::update() { clock_.tick(); }

void App::resolve_pick(int viewport_w, int viewport_h,
                       const VoxelWorld& grid, const uint8_t* materials) {
    if (!pending_pick_) return;
    glm::vec2 px = *pending_pick_;
    pending_pick_.reset();
    glm::mat4 inv_vp = glm::inverse(camera_.view_proj());
    selection_ = pick(viewport_w, viewport_h, px.x, px.y, inv_vp,
                      camera_.position(), grid, materials, config_.march.max_steps);
}
}
