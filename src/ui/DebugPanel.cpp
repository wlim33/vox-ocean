#include "ui/DebugPanel.h"
#include "core/App.h"
#include "voxel/MaterialRegistry.h"
#include "imgui.h"
#include <string>

namespace vox {
void draw_debug_panel(App& app) {
    auto& c = app.config();
    ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
    ImGui::Begin("vox-ocean");
    ImGui::Text("frame dt: %.2f ms", app.clock().delta_seconds() * 1000.0);

    if (app.selection()) {
        const auto& s = *app.selection();
        ImGui::Text("selected: %s (%d, %d, %d)",
                    std::string(vox::material_name((vox::VoxMat)s.material)).c_str(),
                    s.ix, s.iy, s.iz);
    } else {
        ImGui::Text("selected: none");
    }

    if (ImGui::CollapsingHeader("World Edit", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool can_build = app.selection() && app.selection()->has_neighbor;
        if (can_build) {
            const auto& s = *app.selection();
            ImGui::Text("build target: (%d, %d, %d)", s.nx, s.ny, s.nz);
        } else {
            ImGui::TextUnformatted("select a voxel face to build");
        }
        ImGui::BeginDisabled(!can_build);
        if (ImGui::Button("Build Rock"))  app.enqueue_build(VoxMat::Rock);
        ImGui::SameLine();
        if (ImGui::Button("Build Sand"))  app.enqueue_build(VoxMat::SandGrain);
        if (ImGui::Button("Build Water")) app.enqueue_build(VoxMat::Water);
        ImGui::SameLine();
        if (ImGui::Button("Build Lava"))  app.enqueue_build(VoxMat::Lava);
        ImGui::EndDisabled();

        ImGui::Separator();
        bool has_sel = (bool)app.selection();
        ImGui::BeginDisabled(!has_sel);
        if (ImGui::Button("Dig")) app.enqueue_dig();
        ImGui::SameLine();
        if (ImGui::Button("Paint Rock"))  app.enqueue_paint(VoxMat::Rock);
        if (ImGui::Button("Paint Sand"))  app.enqueue_paint(VoxMat::SandGrain);
        ImGui::SameLine();
        if (ImGui::Button("Paint Water")) app.enqueue_paint(VoxMat::Water);
        if (ImGui::Button("Paint Lava"))  app.enqueue_paint(VoxMat::Lava);
        ImGui::EndDisabled();
    }

    if (ImGui::CollapsingHeader("Sky")) {
        ImGui::SliderFloat("sun elev", &c.sky.sun_elevation_rad, 0.0f, 1.57f);
        ImGui::SliderFloat("sun azim", &c.sky.sun_azimuth_rad,  0.0f, 6.283f);
        ImGui::SliderFloat("turbidity",&c.sky.turbidity,        1.0f, 10.0f);
    }
    if (ImGui::CollapsingHeader("Shading")) {
        ImGui::SliderFloat("foam thresh",&c.shading.foam_threshold, 0.0f, 1.0f);
        ImGui::SliderFloat("foam str",   &c.shading.foam_strength,  0.0f, 4.0f);
        ImGui::SliderFloat("fog density",&c.shading.depth_fog_density, 0.0f, 0.5f);
        ImGui::SliderFloat("water ior",  &c.shading.water_ior, 1.0f, 1.6f);
    }
    if (ImGui::CollapsingHeader("Debug view")) {
        const char* names[] = {"final","normal","folding","fresnel","reflection","refraction","sss"};
        ImGui::Combo("view", &app.debug_view, names, IM_ARRAYSIZE(names));
    }
    if (ImGui::CollapsingHeader("Voxels", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& v = c.voxel;
        ImGui::SliderInt("grid extent",       &v.grid_extent,   32,   512);
        ImGui::SliderFloat("voxel size (m)",  &v.voxel_size_m,  0.1f, 2.0f);
        ImGui::SliderFloat("height step (m)", &v.height_step_m, 0.05f, 1.0f);
        ImGui::SliderFloat("base depth (m)",  &v.base_depth_m,  1.0f, 30.0f);
        ImGui::SliderInt("height cells",      &v.height_cells,  16,   256);
        ImGui::InputInt("floor seed",         &v.floor_seed);
    }
    if (ImGui::CollapsingHeader("March", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("backend: %s (restart to change)", c.render.backend.c_str());
        ImGui::Text("grid cells: %d", c.voxel.grid_extent * c.voxel.grid_extent * c.voxel.height_cells);
        ImGui::SliderInt("max steps",      &c.march.max_steps,    32, 4096);
        ImGui::SliderFloat("render scale", &c.march.render_scale, 0.25f, 1.0f);
    }
    if (ImGui::CollapsingHeader("Entity", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("boat", &c.entity.boat_enabled);
        ImGui::SliderFloat("boat speed", &c.entity.boat_speed_mps, 0.0f, 5.0f);
        ImGui::SliderFloat("wake amp",   &c.entity.wake_amp,       0.0f, 2.0f);
    }
    if (ImGui::CollapsingHeader("Kelp", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("kelp", &c.kelp.enabled);
        ImGui::SliderFloat("density",       &c.kelp.density,       0.0f, 0.3f);
        ImGui::SliderFloat("max height",    &c.kelp.max_height_m,  1.0f, 30.0f);
        ImGui::SliderFloat("sway strength", &c.kelp.sway_strength, 0.0f, 4.0f);
        ImGui::SliderFloat("sway ambient",  &c.kelp.sway_ambient,  0.0f, 2.0f);
    }
    if (ImGui::CollapsingHeader("Fish", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("fish", &c.fish.enabled);
        ImGui::SliderInt("schools",    &c.fish.school_count, 0, 32);
        ImGui::SliderInt("per school", &c.fish.per_school,   0, 256);
        ImGui::SliderFloat("speed",    &c.fish.speed_mps,    0.0f, 8.0f);
        ImGui::SliderFloat("depth",    &c.fish.depth_frac,   0.0f, 1.0f);
        ImGui::SliderFloat("spread",   &c.fish.spread_m,     0.0f, 20.0f);
    }
    ImGui::End();
}
}
