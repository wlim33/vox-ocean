#include "core/App.h"
#include "voxel/Brush.h"
#include <algorithm>
#include <utility>
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
            case InputKind::Draw:
                pending_draw_ = glm::vec2{e.x, e.y};
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

void App::resolve_draw(int viewport_w, int viewport_h,
                       const VoxelWorld& grid, const uint8_t* materials) {
    if (!pending_draw_) return;
    glm::vec2 px = *pending_draw_;
    pending_draw_.reset();
    glm::mat4 inv_vp = glm::inverse(camera_.view_proj());
    selection_ = pick(viewport_w, viewport_h, px.x, px.y, inv_vp,
                      camera_.position(), grid, materials, config_.march.max_steps);
    if (!selection_) return;

    uint32_t center = 0;
    VoxMat   mat    = material_;
    switch (tool_) {
        case EditTool::Paint: center = selection_->linear_idx; mat = material_;   break;
        case EditTool::Dig:   center = selection_->linear_idx; mat = VoxMat::Air; break;
        case EditTool::Build:
            if (!selection_->has_neighbor) return;
            center = selection_->neighbor_idx; mat = material_;                   break;
    }
    sphere_cells(grid, center, brush_radius_, draw_cells_);
    for (uint32_t cell : draw_cells_)
        pending_edits_.push_back({cell, (uint8_t)mat});
}

void App::set_brush_radius(int r) { brush_radius_ = std::clamp(r, 0, kMaxBrushRadius); }

void App::enqueue_build(VoxMat m) {
    if (!selection_ || !selection_->has_neighbor) return;
    pending_edits_.push_back({selection_->neighbor_idx, (uint8_t)m});
}

void App::enqueue_paint(VoxMat m) {
    if (!selection_) return;
    pending_edits_.push_back({selection_->linear_idx, (uint8_t)m});
}

void App::enqueue_dig() { enqueue_paint(VoxMat::Air); }

std::vector<UserEdit> App::drain_pending_edits() {
    return std::exchange(pending_edits_, {});
}
}
