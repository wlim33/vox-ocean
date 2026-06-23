#include "ui/ToolPalette.h"
#include "core/App.h"
#include "voxel/MaterialRegistry.h"
#include "imgui.h"
#include <iterator>

namespace vox {
void draw_tool_palette(App& app) {
    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(220.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Tools", nullptr, ImGuiWindowFlags_NoCollapse);

    ImGui::TextUnformatted("Tool");
    EditTool tool = app.tool();
    if (ImGui::RadioButton("Dig",   tool == EditTool::Dig))   app.set_tool(EditTool::Dig);
    ImGui::SameLine();
    if (ImGui::RadioButton("Paint", tool == EditTool::Paint)) app.set_tool(EditTool::Paint);
    ImGui::SameLine();
    if (ImGui::RadioButton("Build", tool == EditTool::Build)) app.set_tool(EditTool::Build);

    ImGui::Separator();
    ImGui::TextUnformatted("Material");
    ImGui::BeginDisabled(app.tool() == EditTool::Dig);   // Dig always writes Air
    const struct { const char* label; VoxMat mat; } palette[] = {
        {"Rock",  VoxMat::Rock},      {"Sand", VoxMat::SandGrain},
        {"Water", VoxMat::Water},     {"Lava", VoxMat::Lava},
    };
    VoxMat mat = app.material();
    for (size_t i = 0; i < std::size(palette); ++i) {
        if (ImGui::RadioButton(palette[i].label, mat == palette[i].mat))
            app.set_material(palette[i].mat);
        if (i % 2 == 0) ImGui::SameLine();   // two per row
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    int r = app.brush_radius();
    if (ImGui::SliderInt("Brush size", &r, 0, App::kMaxBrushRadius)) app.set_brush_radius(r);

    ImGui::End();
}
}
