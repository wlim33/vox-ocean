#include "engine/Engine.h"
#include "core/App.h"
#include "core/InputBridge.h"
#include "entity/Ecosystem.h"
#include "entity/StampBudget.h"
#include "voxel/VoxelWorld.h"
#include "gpu/MetalContext.h"
#include "gpu/PipelineCache.h"
#include "ocean/Simulation.h"
#include "ocean/WaterModel.h"
#include "world/World.h"
#include "world/RenderFrame.h"
#include "render/SkyRenderer.h"
#include "voxel/DenseVoxelField.h"
#include "voxel/RippleSim.h"
#include "render/IVoxelRenderer.h"
#include "core/CameraView.h"
#include "core/AxisCamera.h"
#include "gpu/Buffer.h"
#include "gpu/Texture.h"
#include "io/Image.h"
#include "io/PngWriter.h"
#include "io/ContactSheet.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdio>
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
    World world;
    WaterModel water;   // CPU analytical surface; replaces the GPU height readback
    RenderFrame frame;
#ifndef NDEBUG
    std::vector<uint8_t> applied_dbg;   // debug-only: replica rebuilt from the edit stream
#endif
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

// Evolve the simulation by one frame and voxelize the world into the field's
// grid/surface textures. Camera-independent. `sim_time` is absolute seconds;
// `dt` is the step. Shared by the live render loop and the headless snapshot.
static void advance_and_voxelize(Engine* e, id<MTLCommandBuffer> cb,
                                 float sim_time, float dt) {
    const Config& cfg = e->app->config();
    e->sky.bake_cubemap_if_dirty(e->ctx, (__bridge void*)cb, cfg);
    e->sim.rebuild_if_dirty(e->ctx, cfg);
    e->world.configure(cfg);
    e->field.rebuild_if_dirty(e->ctx, cfg);
    e->ripple.rebuild_if_dirty(e->ctx, cfg);
    e->field.ensure_capacity(e->ctx, cfg);
    e->field.upload_terrain_if_dirty((__bridge void*)cb, e->world.terrain_cells());
    e->ripple.upload_zero_if_dirty((__bridge void*)cb);

    e->ecosystem.rebuild_if_dirty(cfg, e->world);
    e->water.configure(cfg.wave);
    e->ecosystem.update(cfg, dt, sim_time,
        [&](float x, float z) { return e->water.height_at(x, z, sim_time); },
        e->world);

    std::vector<RippleSplash> wake;
    glm::vec2 stern;
    if (e->ecosystem.shed_boat_wake(cfg, stern)) {
        float half = 0.5f * cfg.voxel.grid_extent * cfg.voxel.voxel_size_m;
        wake.push_back({ (stern.x + half) / cfg.voxel.voxel_size_m,
                         (stern.y + half) / cfg.voxel.voxel_size_m,
                         1.5f, -cfg.entity.wake_amp });
    }
    e->world.begin_frame();
    e->ecosystem.build_stamp(cfg, e->world.grid(), e->stamp);
    e->world.ingest(e->stamp);   // composite entities into the authoritative grid

    e->world.build_edits(e->frame.edits);
#ifndef NDEBUG
    // Validate the edit stream reconstructs the discrete world (step 4 moves
    // this apply onto the GPU). applied_dbg is only ever mutated via apply()
    // or a wholesale resync — never copied from cells() mid-stream.
    if (e->frame.edits.resync) e->applied_dbg = e->world.cells();
    else vox::apply_edits(e->applied_dbg, e->frame.edits);
    assert(e->applied_dbg == e->world.cells()
           && "EditList stream diverged from World::cells()");
#endif

    id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
    e->sim.encode((__bridge void*)ce, sim_time, cfg);
    e->ripple.encode((__bridge void*)ce, cfg, dt, wake.data(), (int)wake.size());
    e->field.encode_fill((__bridge void*)ce, cfg, e->sim.data(), e->sim.count(),
                         e->ripple.front_texture(), e->frame_index);
    e->field.encode_destamp((__bridge void*)ce, cfg, e->frame_index);
    e->field.encode_stamp((__bridge void*)ce, cfg, e->stamp, e->frame_index);
    e->field.encode_verify((__bridge void*)ce, cfg, e->sim.data(), e->sim.count(),
                           e->ripple.front_texture(), e->stamp, e->frame_index);
    [ce endEncoding];

    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    e->sim.encode_mipgen((__bridge void*)blit, cfg);
    [blit endEncoding];
}

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

    CGSize dsz = view.drawableSize;
    e->renderer->resize(e->ctx, (int)dsz.width, (int)dsz.height, e->app->config());

    float sim_time = (float)e->app->clock().total_seconds();
    float dt = (float)e->app->clock().delta_seconds();
    advance_and_voxelize(e, cb, sim_time, dt);

    CameraView cam = e->app->camera().camera_view();
    e->renderer->encode((__bridge void*)cb, e->field, cam, e->app->config(),
                        e->sky, e->frame_index);

    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
    e->sky.encode_full_screen((__bridge void*)enc, cam, e->app->config());
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
static std::vector<AxisShot> snapshot_parse_views(const std::string& csv) {
    auto one = [](const std::string& s) -> std::vector<AxisShot> {
        if (s == "top")                  return {{ViewAxis::Y, true}};
        if (s == "bottom")               return {{ViewAxis::Y, false}};
        if (s == "front")                return {{ViewAxis::Z, true}};
        if (s == "back")                 return {{ViewAxis::Z, false}};
        if (s == "side" || s == "right") return {{ViewAxis::X, true}};
        if (s == "left")                 return {{ViewAxis::X, false}};
        if (s == "all")                  return {{ViewAxis::Y, true}, {ViewAxis::Z, true}, {ViewAxis::X, true},
                                                 {ViewAxis::Y, false}, {ViewAxis::Z, false}, {ViewAxis::X, false}};
        return {};
    };
    std::vector<AxisShot> out;
    std::stringstream ss(csv);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        while (!tok.empty() && tok.front() == ' ') tok.erase(tok.begin());
        while (!tok.empty() && tok.back() == ' ')  tok.pop_back();
        for (auto& v : one(tok)) out.push_back(v);
    }
    if (out.empty())
        out = {{ViewAxis::Y, true}, {ViewAxis::Z, true}, {ViewAxis::X, true}};
    return out;
}

static std::string snapshot_label(AxisShot s) {
    switch (s.axis) {
        case ViewAxis::Y: return s.from_positive ? "TOP +Y"   : "BOT -Y";
        case ViewAxis::Z: return s.from_positive ? "FRONT +Z" : "BACK -Z";
        case ViewAxis::X: return s.from_positive ? "SIDE +X"  : "SIDE -X";
    }
    return "?";
}

static RgbImage snapshot_readback(const MetalContext& ctx, id<MTLTexture> tex, int w, int h) {
    Buffer buf = make_buffer(ctx, (size_t)w * h * 4, /*shared=*/true);
    id<MTLCommandQueue> q = (__bridge id<MTLCommandQueue>)ctx.queue;
    id<MTLCommandBuffer> cb = [q commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    [blit copyFromTexture:tex sourceSlice:0 sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(w, h, 1)
                 toBuffer:(__bridge id<MTLBuffer>)buf.handle destinationOffset:0
        destinationBytesPerRow:(NSUInteger)w * 4 destinationBytesPerImage:(NSUInteger)w * h * 4];
    [blit endEncoding];
    [cb commit];
    [cb waitUntilCompleted];

    RgbImage img{w, h, std::vector<uint8_t>((size_t)w * h * 3)};
    const uint8_t* src = (const uint8_t*)buf.cpu_ptr;
    for (int i = 0; i < w * h; ++i) {       // BGRA8 -> RGB
        img.rgb[i * 3 + 0] = src[i * 4 + 2];
        img.rgb[i * 3 + 1] = src[i * 4 + 1];
        img.rgb[i * 3 + 2] = src[i * 4 + 0];
    }
    destroy_buffer(buf);
    return img;
}

int engine_snapshot(const char* config_path, const char* overrides,
                    const char* out_path, const char* views_csv,
                    int cell_size, bool separate, int warmup_frames) {
    Engine* e = engine_create(config_path, overrides);
    const Config& cfg = e->app->config();
    id<MTLDevice> dev = (__bridge id<MTLDevice>)e->ctx.device;
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)e->ctx.queue;

    // 1. Fixed-dt warmup: settle FFT / ripple / entities to a reproducible state.
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < std::max(1, warmup_frames); ++i) {
        @autoreleasepool {
            id<MTLCommandBuffer> cb = [queue commandBuffer];
            advance_and_voxelize(e, cb, (float)i * dt, dt);
            [cb commit];
            [cb waitUntilCompleted];
        }
        e->frame_index++;
    }

    // 2. Per-axis offscreen render of the settled world grid.
    VoxelGridDesc grid{cfg.voxel.grid_extent, cfg.voxel.height_cells,
                       cfg.voxel.voxel_size_m, cfg.voxel.height_step_m, cfg.voxel.base_depth_m};
    const float spanY = vg_world_top_y(grid) + cfg.voxel.base_depth_m;
    const float pad = 0.04f * (2.0f * vg_half_patch(grid) + spanY);
    const int S = std::max(8, cell_size);

    // Depth attachment to satisfy the sky/composite PSOs (which declare
    // Depth32Float to match the live drawable). Never read; the depth state is
    // compare-Always / no-write. Reused across axes (all SxS).
    MTLTextureDescriptor* dtd = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:S height:S mipmapped:NO];
    dtd.usage = MTLTextureUsageRenderTarget;
    dtd.storageMode = MTLStorageModePrivate;
    id<MTLTexture> depthTex = [dev newTextureWithDescriptor:dtd];

    std::vector<AxisShot> shots = snapshot_parse_views(views_csv ? views_csv : "");
    std::vector<RgbImage> cells;
    std::vector<std::string> labels;
    bool sep_ok = true;

    for (AxisShot shot : shots) {
        @autoreleasepool {
        e->renderer->resize(e->ctx, S, S, cfg);
        Texture color = make_texture_2d(e->ctx, (uint32_t)S, (uint32_t)S,
                                        TexFormat::BGRA8Unorm_sRGB,
                                        /*storage_write=*/false, /*render_target=*/true);
        CameraView cam = axis_ortho_view(shot, grid, pad, 1.0f);

        id<MTLCommandBuffer> cb = [queue commandBuffer];
        e->renderer->encode((__bridge void*)cb, e->field, cam, cfg, e->sky, e->frame_index);

        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = (__bridge id<MTLTexture>)color.handle;
        rp.colorAttachments[0].loadAction = MTLLoadActionClear;
        rp.colorAttachments[0].clearColor = MTLClearColorMake(0.02, 0.05, 0.10, 1.0);
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        rp.depthAttachment.texture = depthTex;
        rp.depthAttachment.loadAction = MTLLoadActionClear;
        rp.depthAttachment.clearDepth = 1.0;
        rp.depthAttachment.storeAction = MTLStoreActionDontCare;

        id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
        e->sky.encode_full_screen((__bridge void*)enc, cam, cfg);
        e->renderer->draw_into_drawable((__bridge void*)enc);
        [enc endEncoding];
        [cb commit];
        [cb waitUntilCompleted];

        RgbImage img = snapshot_readback(e->ctx, (__bridge id<MTLTexture>)color.handle, S, S);
        destroy_texture(color);

        if (separate) {
            std::string base(out_path);
            auto dot = base.rfind('.');
            std::string stem = (dot == std::string::npos) ? base : base.substr(0, dot);
            std::string tag;
            switch (shot.axis) {
                case ViewAxis::Y: tag = shot.from_positive ? "top"   : "bottom"; break;
                case ViewAxis::Z: tag = shot.from_positive ? "front" : "back";   break;
                case ViewAxis::X: tag = shot.from_positive ? "right" : "left";   break;
            }
            sep_ok &= write_png(stem + "." + tag + ".png", img);
        }
        cells.push_back(std::move(img));
        labels.push_back(snapshot_label(shot));
        } // @autoreleasepool
    }
    depthTex = nil;

    RgbImage sheet = make_contact_sheet(cells, labels);
    bool ok = write_png(out_path, sheet) && sep_ok;
    fprintf(stderr, "[vox] snapshot %s -> %s (%dx%d, %zu views)\n",
            ok ? "wrote" : "FAILED", out_path, sheet.w, sheet.h, shots.size());

    engine_destroy(e);
    return ok ? 0 : 1;
}

} // namespace vox
