#include "ui/DebugPanel.h"
#include "core/App.h"
#include "imgui.h"

namespace vox {
void draw_debug_panel(App& app) {
    auto& c = app.config();
    ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("vox-ocean");
    ImGui::Text("frame dt: %.2f ms", app.clock().delta_seconds() * 1000.0);

    if (ImGui::CollapsingHeader("Cascades", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderInt("count", &c.cascade_count, 1, 4);
        for (int i = 0; i < c.cascade_count; ++i) {
            ImGui::PushID(i);
            ImGui::Text("Cascade %d", i);
            ImGui::SliderFloat("size m", &c.cascades[i].size_m, 1.0f, 500.0f);
            ImGui::PopID();
        }
    }
    if (ImGui::CollapsingHeader("Wave")) {
        ImGui::SliderFloat("wind speed", &c.wave.wind_speed_mps, 0.0f, 30.0f);
        ImGui::SliderFloat("wind dir",   &c.wave.wind_dir_rad,  0.0f, 6.283f);
        ImGui::SliderFloat("choppiness", &c.wave.choppiness,    0.0f, 2.0f);
        ImGui::SliderFloat("swell",      &c.wave.swell,         0.0f, 1.0f);
        ImGui::SliderFloat("amplitude",  &c.wave.amplitude,     0.1f, 10000.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("max wavelength", &c.wave.max_wavelength_m, 0.0f, 300.0f);
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
        ImGui::SliderInt("max steps",      &c.march.max_steps,    32, 4096);
        ImGui::SliderFloat("render scale", &c.march.render_scale, 0.25f, 1.0f);
    }
    if (ImGui::CollapsingHeader("Ripple", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("wave speed",   &c.ripple.wave_speed_mps, 0.1f, 20.0f);
        ImGui::SliderFloat("damping",      &c.ripple.damping,        0.80f, 1.0f);
        ImGui::SliderFloat("rain rate",    &c.ripple.rain_rate,      0.0f, 200.0f);
        ImGui::SliderFloat("ripple foam",  &c.ripple.foam,           0.0f, 4.0f);
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
