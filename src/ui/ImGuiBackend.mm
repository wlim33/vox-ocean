#import "ui/ImGuiBackend.h"
#import "gpu/MetalContext.h"
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#include <TargetConditionals.h>
#include "imgui.h"
#include "imgui_impl_metal.h"
#if TARGET_OS_OSX
#include "imgui_impl_osx.h"
#else
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace vox {

#if !TARGET_OS_OSX
// On iOS there is no ImGui_ImplOSX_NewFrame to supply per-frame display
// metrics, so we track wall-clock time ourselves to feed io.DeltaTime.
static double g_last_time = 0.0;
#endif

void ImGuiBackend::init(const MetalContext& ctx, void* mtkview) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplMetal_Init((__bridge id<MTLDevice>)ctx.device);
    MTKView* v = (__bridge MTKView*)mtkview;
#if TARGET_OS_OSX
    ImGui_ImplOSX_Init(v);
#else
    (void)v;
    g_last_time = 0.0;
#endif
}
void ImGuiBackend::shutdown() {
    ImGui_ImplMetal_Shutdown();
#if TARGET_OS_OSX
    ImGui_ImplOSX_Shutdown();
#endif
    ImGui::DestroyContext();
}
void ImGuiBackend::begin_frame(void* mtkview) {
    MTKView* v = (__bridge MTKView*)mtkview;
    ImGui_ImplMetal_NewFrame(v.currentRenderPassDescriptor);
#if TARGET_OS_OSX
    ImGui_ImplOSX_NewFrame(v);
#else
    // Replicate the per-frame setup ImGui_ImplOSX_NewFrame would do: display
    // size in points, framebuffer scale, and a positive delta time (ImGui
    // asserts DeltaTime > 0).
    ImGuiIO& io = ImGui::GetIO();
    CGSize bounds = v.bounds.size;
    CGFloat scale = v.contentScaleFactor;
    io.DisplaySize = ImVec2((float)bounds.width, (float)bounds.height);
    io.DisplayFramebufferScale = ImVec2((float)scale, (float)scale);
    double now = CACurrentMediaTime();   // monotonic; wall-clock jumps would feed ImGui a negative dt
    double dt = (g_last_time > 0.0) ? (now - g_last_time) : (1.0 / 60.0);
    if (dt < 1.0 / 1000.0) dt = 1.0 / 1000.0;
    io.DeltaTime = (float)dt;
    g_last_time = now;
#endif
    ImGui::NewFrame();
}
void ImGuiBackend::render(void* command_buffer, void*, void* render_encoder) {
    ImGui::Render();
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(),
        (__bridge id<MTLCommandBuffer>)command_buffer,
        (__bridge id<MTLRenderCommandEncoder>)render_encoder);
}
}
