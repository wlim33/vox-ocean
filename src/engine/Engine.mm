#include "engine/Engine.h"
#include "core/App.h"
#include "core/InputBridge.h"
#include "entity/Ecosystem.h"
#include "entity/StampBudget.h"
#include "voxel/VoxelWorld.h"
#include "gpu/MetalContext.h"
#include "gpu/PipelineCache.h"
#include "ocean/Simulation.h"
#include "render/SkyRenderer.h"
#include "voxel/DenseVoxelField.h"
#include "voxel/RippleSim.h"
#include "render/IVoxelRenderer.h"
#include "render/RendererFactory.h"
#include "ui/ImGuiBackend.h"
#include "ui/DebugPanel.h"
#include "bench/BenchmarkHarness.h"
#include "bench/BenchCameraPath.h"
#import <MetalKit/MetalKit.h>
#include <chrono>
#include <fstream>
#include <sstream>
#include <memory>
#include <string>
#include <vector>

namespace vox {

class Engine {
public:
    MetalContext ctx;
    PipelineCache cache;
    InputBridge input;
    std::unique_ptr<App> app;
    Simulation sim;
    SkyRenderer sky;
    DenseVoxelField field;
    RippleSim ripple;
    std::unique_ptr<IVoxelRenderer> renderer;
    ImGuiBackend imgui;
    BenchmarkHarness bench;
    Ecosystem ecosystem;
    StampList stamp;
    // Bounds CPU frames-in-flight to config.max_in_flight_frames; created
    // lazily on the first render. signaled in the command-buffer completed
    // handler so the CPU blocks once that many frames are still on the GPU.
    dispatch_semaphore_t inflight = nil;
    // Swift owns the view; __weak so a torn-down view reads as nil in
    // engine_render instead of being kept alive (or dangling) by the engine.
    __weak MTKView* view = nil;
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
    e->field.init(e->ctx, e->cache);
    e->ripple.init(e->ctx, e->cache);
    e->renderer = make_renderer(e->app->config().render.backend);
    e->renderer->init(e->ctx, e->cache);
    e->bench.start(e->app->config(), config_hash(e->app->config()));
    return e;
}

// Contract: only call at process teardown. In-flight completed handlers hold
// a raw Engine*; destroying mid-session would require draining the queue first.
void engine_destroy(Engine* e) { delete e; }

void engine_attach_view(Engine* e, void* mtk_view) {
    if (e->imgui_ready) return;  // one-shot: re-attach would double-init ImGui
    e->view = (__bridge MTKView*)mtk_view;
    e->view.device = (__bridge id<MTLDevice>)e->ctx.device;
    e->view.colorPixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    e->view.depthStencilPixelFormat = MTLPixelFormatDepth32Float;
    e->view.clearColor = MTLClearColorMake(0.02, 0.05, 0.10, 1.0);
    e->imgui.init(e->ctx, (__bridge void*)e->view);
    e->imgui_ready = true;
    // Seed the camera aspect; before layout drawableSize can be 0x0, in which
    // case the first Resize event corrects it.
    CGSize ds = e->view.drawableSize;
    if (ds.width > 0 && ds.height > 0)
        e->app->camera().set_aspect((float)(ds.width / ds.height));
}

void engine_resize(Engine* e, int w, int h) {
    InputEvent ev{}; ev.kind = InputKind::Resize; ev.width = w; ev.height = h;
    e->input.push(ev);
}

void engine_push_input(Engine* e, InputEvent ev) { e->input.push(ev); }
bool engine_bench_should_exit(Engine* e) { return e->bench.should_exit(); }

void engine_render(Engine* e) {
    MTKView* view = e->view;
    if (!view) return;

    // Created once; not runtime-tunable. Recreate if max_in_flight_frames is
    // ever exposed in the debug panel.
    if (!e->inflight)
        e->inflight = dispatch_semaphore_create(
            MAX(1, MIN(3, e->app->config().max_in_flight_frames)));
    dispatch_semaphore_wait(e->inflight, DISPATCH_TIME_FOREVER);

    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)e->ctx.queue;
    id<MTLCommandBuffer> cb = [queue commandBuffer];

    // Drawable acquisition can block on a free drawable; bracket it to measure
    // the stall so a CPU-bound vs. present-bound frame is distinguishable.
    auto wait_t0 = std::chrono::steady_clock::now();
    MTLRenderPassDescriptor* rp = view.currentRenderPassDescriptor;
    double wait_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - wait_t0).count();
    if (!rp) { [cb commit]; dispatch_semaphore_signal(e->inflight); return; }

    auto cpu_t0 = std::chrono::steady_clock::now();
    if (e->bench.active())
        apply_bench_path(e->app->camera(), e->app->config().bench.camera_path,
                         e->bench.current_frame());
    e->app->handle_input(e->input);
    e->app->update();

    const bool ui = e->imgui_ready;
    if (ui) { e->imgui.begin_frame((__bridge void*)view); draw_debug_panel(*e->app); }

    e->sky.bake_cubemap_if_dirty(e->ctx, (__bridge void*)cb, e->app->config());
    e->sim.rebuild_if_dirty(e->ctx, e->app->config());
    e->field.rebuild_if_dirty(e->ctx, e->app->config());
    e->ripple.rebuild_if_dirty(e->ctx, e->app->config());
    CGSize dsz = view.drawableSize;
    e->renderer->resize(e->ctx, (int)dsz.width, (int)dsz.height,
                        e->app->config());
    e->field.ensure_capacity(e->ctx, e->app->config());
    e->field.upload_terrain_if_dirty((__bridge void*)cb);
    e->ripple.upload_zero_if_dirty((__bridge void*)cb);
    float sim_time = (float)e->app->clock().total_seconds();

    const Config& cfg = e->app->config();
    float dt = (float)e->app->clock().delta_seconds();
    e->ecosystem.rebuild_if_dirty(cfg);
    e->ecosystem.update(cfg, dt, sim_time,
        [&](float x, float z) { return e->field.height_at(x, z, cfg, e->frame_index); });

    std::vector<RippleSplash> wake;
    glm::vec2 stern;
    if (e->ecosystem.shed_boat_wake(cfg, stern)) {
        float half = 0.5f * cfg.voxel.grid_extent * cfg.voxel.voxel_size_m;
        wake.push_back({ (stern.x + half) / cfg.voxel.voxel_size_m,
                         (stern.y + half) / cfg.voxel.voxel_size_m,
                         1.5f, -cfg.entity.wake_amp });
    }
    VoxelWorld world({cfg.voxel.grid_extent, cfg.voxel.height_cells,
                      cfg.voxel.voxel_size_m, cfg.voxel.height_step_m,
                      cfg.voxel.base_depth_m});
    e->ecosystem.build_stamp(cfg, world, e->stamp);

    id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
    e->sim.encode((__bridge void*)ce, sim_time, e->app->config());
    e->ripple.encode((__bridge void*)ce, e->app->config(),
                     (float)e->app->clock().delta_seconds(),
                     wake.data(), (int)wake.size());
    e->field.encode_fill((__bridge void*)ce, e->app->config(), e->sim.data(), e->sim.count(), e->ripple.front_texture(), e->frame_index);
    e->field.encode_destamp((__bridge void*)ce, e->app->config(), e->frame_index);
    e->field.encode_stamp((__bridge void*)ce, e->app->config(), e->stamp, e->frame_index);
    e->field.encode_verify((__bridge void*)ce, e->app->config(), e->sim.data(), e->sim.count(),
                           e->ripple.front_texture(), e->stamp, e->frame_index);
    [ce endEncoding];

    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    e->sim.encode_mipgen((__bridge void*)blit, e->app->config());
    e->field.encode_readback((__bridge void*)blit, e->app->config(), e->frame_index);
    [blit endEncoding];

    e->renderer->encode((__bridge void*)cb, e->field, e->app->camera(), e->app->config(),
                        e->sky, e->frame_index);

    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
    e->sky.encode_full_screen((__bridge void*)enc, e->app->camera(), e->app->config());
    e->renderer->draw_into_drawable((__bridge void*)enc);
    if (ui) e->imgui.render((__bridge void*)cb, (__bridge void*)rp, (__bridge void*)enc);
    [enc endEncoding];

    if (view.currentDrawable) [cb presentDrawable:view.currentDrawable];

    // Completed handler runs on a background queue: capture POD by value and
    // the engine by raw pointer (it outlives the queue in practice). bench's
    // Handlers on one queue run serially, so out_ is single-threaded; the
    // frame counter is atomic for the concurrent main-thread polls.
    double cpu_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - cpu_t0).count();
    dispatch_semaphore_t inflight = e->inflight;
    [cb addCompletedHandler:^(id<MTLCommandBuffer> b) {
        double gpu_ms = (b.GPUEndTime - b.GPUStartTime) * 1000.0;
        e->bench.record({ e->bench.current_frame(), cpu_ms, gpu_ms, wait_ms });
        dispatch_semaphore_signal(inflight);
    }];
    [cb commit];
    e->frame_index++;
}
} // namespace vox
