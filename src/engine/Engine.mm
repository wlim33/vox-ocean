#include "engine/Engine.h"
#include "core/App.h"
#include "core/InputBridge.h"
#include "gpu/MetalContext.h"
#include "gpu/PipelineCache.h"
#include "ocean/Simulation.h"
#include "render/SkyRenderer.h"
#include "ui/ImGuiBackend.h"
#include "ui/DebugPanel.h"
#import <MetalKit/MetalKit.h>
#include <chrono>
#include <fstream>
#include <sstream>
#include <memory>
#include <string>
#include <vector>

// TODO(task-13): bench harness wiring returns when src/bench is ported.

namespace vox {

class Engine {
public:
    MetalContext ctx;
    PipelineCache cache;
    InputBridge input;
    std::unique_ptr<App> app;
    Simulation sim;
    SkyRenderer sky;
    ImGuiBackend imgui;
    MTKView* view = nil;           // not owned: Swift owns the view
    int frame_index = 0;
    bool imgui_ready = false;
};

static std::string slurp(const char* path) {
    std::ifstream in(path); std::stringstream ss; ss << in.rdbuf(); return ss.str();
}

Engine* engine_create(const char* config_path, const char* overrides) {
    auto* e = new Engine();
    e->ctx = create_metal_context();

    std::string toml;
    if (config_path && *config_path) toml = slurp(config_path);
    if (toml.empty()) {
        NSString* p = [[NSBundle mainBundle] pathForResource:@"default-config" ofType:@"toml"];
        if (p) toml = slurp(p.UTF8String);
    }
    std::vector<std::string> ov;
    if (overrides) {
        std::stringstream ss(overrides); std::string line;
        while (std::getline(ss, line)) if (!line.empty()) ov.push_back(line);
    }
    auto load = apply_overrides(load_config_from_string(toml), ov);
    e->app = std::make_unique<App>(load.config);

    e->sky.init(e->ctx, e->cache);
    e->sim.init(e->ctx, e->cache, e->app->config());
    // TODO(task-13): e->bench.start(e->app->config(), config_hash(e->app->config()));
    return e;
}

void engine_destroy(Engine* e) { delete e; }

void engine_attach_view(Engine* e, void* mtk_view) {
    e->view = (__bridge MTKView*)mtk_view;
    e->view.device = (__bridge id<MTLDevice>)e->ctx.device;
    e->view.colorPixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    e->view.depthStencilPixelFormat = MTLPixelFormatDepth32Float;
    e->view.clearColor = MTLClearColorMake(0.02, 0.05, 0.10, 1.0);
    e->imgui.init(e->ctx, (__bridge void*)e->view);
    e->imgui_ready = true;
}

void engine_resize(Engine* e, int w, int h) {
    InputEvent ev{}; ev.kind = InputKind::Resize; ev.width = w; ev.height = h;
    e->input.push(ev);
}

void engine_push_input(Engine* e, InputEvent ev) { e->input.push(ev); }
bool engine_bench_should_exit(Engine* e) { (void)e; return false; }  // TODO(task-13)

void engine_render(Engine* e) {
    MTKView* view = e->view;
    if (!view) return;
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)e->ctx.queue;
    id<MTLCommandBuffer> cb = [queue commandBuffer];

    MTLRenderPassDescriptor* rp = view.currentRenderPassDescriptor;
    if (!rp) { [cb commit]; return; }

    // TODO(task-13): bench camera path + frame timing record.
    e->app->handle_input(e->input);
    e->app->update();

    const bool ui = e->imgui_ready;
    if (ui) { e->imgui.begin_frame((__bridge void*)view); draw_debug_panel(*e->app); }

    e->sky.bake_cubemap_if_dirty(e->ctx, (__bridge void*)cb, e->app->config());
    e->sim.rebuild_if_dirty(e->ctx, e->app->config());
    float sim_time = (float)e->app->clock().total_seconds();

    id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
    e->sim.encode((__bridge void*)ce, sim_time, e->app->config());
    [ce endEncoding];

    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    e->sim.encode_mipgen((__bridge void*)blit, e->app->config());
    [blit endEncoding];

    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
    e->sky.encode_full_screen((__bridge void*)enc, e->app->camera(), e->app->config());
    // Task 11 inserts the voxel draw here.
    if (ui) e->imgui.render((__bridge void*)cb, (__bridge void*)rp, (__bridge void*)enc);
    [enc endEncoding];

    if (view.currentDrawable) [cb presentDrawable:view.currentDrawable];
    [cb commit];
    e->frame_index++;
}
} // namespace vox
