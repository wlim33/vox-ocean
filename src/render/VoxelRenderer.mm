#import "render/VoxelRenderer.h"
#import "render/SkyRenderer.h"
#import "ocean/Cascade.h"
#import "core/OrbitCamera.h"
#import "core/Config.h"
#import "gpu/MetalContext.h"
#import "gpu/PipelineCache.h"
#import "voxel/VoxelWorld.h"
#import "voxel/FloorGen.h"
#import "voxel/Ripple.h"
#import "shader_types.h"
#import <Metal/Metal.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/mat4x4.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace vox {

void VoxelRenderer::init(const MetalContext& ctx, PipelineCache& cache) {
    pso_fill_ = cache.compute_pso(ctx, "world_fill");
    pso_ripple_ = cache.compute_pso(ctx, "ripple_step");
    pso_stamp_ = cache.compute_pso(ctx, "stamp_cells");

    // March pass: fullscreen triangle into the offscreen target (no depth, no
    // blend — misses write alpha 0 so the composite shows the sky).
    RenderPSODesc md;
    md.vertex_fn = "march_vs";
    md.fragment_fn = "march_fs";
    md.depth_pixel_format = 0;   // offscreen march pass has no depth attachment
    md.blending = false;
    pso_march_ = cache.render_pso(ctx, md);

    // Composite into the drawable pass — which owns a depth buffer, so the PSO
    // must declare Depth32Float. Alpha-blend over the sky.
    RenderPSODesc cd;
    cd.vertex_fn = "march_composite_vs";
    cd.fragment_fn = "march_composite_fs";
    cd.depth_pixel_format = (unsigned)MTLPixelFormatDepth32Float;
    cd.blending = true;
    pso_composite_ = cache.render_pso(ctx, cd);

    id<MTLDevice> dev = (__bridge id<MTLDevice>)ctx.device;
    MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
    dd.depthCompareFunction = MTLCompareFunctionAlways;
    dd.depthWriteEnabled = NO;
    dss_off_ = (__bridge_retained void*)[dev newDepthStencilStateWithDescriptor:dd];

    for (int i = 0; i < RING; ++i) {
        fill_uniforms_[i]  = make_buffer(ctx, sizeof(WorldFillUniforms), true);
        march_uniforms_[i] = make_buffer(ctx, sizeof(MarchUniforms), true);
        ripple_uniforms_[i] = make_buffer(ctx, sizeof(RippleUniforms), true);
        splash_buf_[i]      = make_buffer(ctx, sizeof(RippleSplash) * MAX_SPLASHES, true);
        stamp_uniforms_[i]  = make_buffer(ctx, sizeof(StampUniforms), true);
        stamp_cells_[i]     = make_buffer(ctx, sizeof(uint32_t) * MAX_STAMP_CELLS, true);
    }
}

void VoxelRenderer::rebuild_if_dirty(const MetalContext& ctx, const Config& cfg) {
    int extent = cfg.voxel.grid_extent;
    int hc     = cfg.voxel.height_cells;
    int seed   = cfg.voxel.floor_seed;
    if (extent == built_extent_ && hc == built_height_cells_ && seed == built_seed_
        && world_grid_.handle)
        return;

    destroy_texture(terrain_grid_);
    destroy_texture(world_grid_);
    destroy_texture(surface_tex_);
    destroy_buffer(terrain_staging_);
    destroy_buffer(ripple_zero_staging_);

    // terrain_grid_ is blit-uploaded once then read-only; world_grid_ is
    // compute-written each frame; surface_tex_ carries per-column water state.
    terrain_grid_ = make_texture_3d(ctx, (uint32_t)extent, (uint32_t)hc, (uint32_t)extent,
                                    TexFormat::R8Uint, /*storage_write=*/false);
    world_grid_   = make_texture_3d(ctx, (uint32_t)extent, (uint32_t)hc, (uint32_t)extent,
                                    TexFormat::R8Uint, /*storage_write=*/true);
    surface_tex_  = make_texture_2d(ctx, (uint32_t)extent, (uint32_t)extent,
                                    TexFormat::RG32F);

    // Ripple ping-pong ring is sized by extent only; reset phase on rebuild.
    for (int i = 0; i < 3; ++i) {
        destroy_texture(ripple_[i]);
        ripple_[i] = make_texture_2d(ctx, (uint32_t)extent, (uint32_t)extent, TexFormat::R32F);
    }
    ripple_phase_ = 0;

    // Surface readback ring: CPU-readable buffers for water_height_at.
    // Zeroed so the boat reads height 0 until real data lands.
    for (int i = 0; i < RING; ++i) {
        destroy_buffer(surface_readback_[i]);
        surface_readback_[i] = make_buffer(ctx, (size_t)extent * extent * 2 * sizeof(float), true);
        std::memset(surface_readback_[i].cpu_ptr, 0, surface_readback_[i].size);
    }

    // Zero-fill staging buffer for the ripple ring; blitted into all three
    // textures by encode_terrain_upload_if_dirty to avoid undefined Metal
    // contents (NaN would propagate through the laplacian and never decay).
    size_t ripple_bytes = (size_t)extent * (size_t)extent * sizeof(float);
    ripple_zero_staging_ = make_buffer(ctx, ripple_bytes, true);
    std::memset(ripple_zero_staging_.cpu_ptr, 0, ripple_bytes);
    ripple_dirty_ = true;

    VoxelWorld world({ extent, hc, cfg.voxel.voxel_size_m, cfg.voxel.height_step_m,
                       cfg.voxel.base_depth_m });
    std::vector<FloorColumn> floor = generate_floor({ extent, hc, (uint32_t)seed });

    size_t cells = (size_t)world.cells();
    terrain_staging_ = make_buffer(ctx, cells, true);
    uint8_t* dst = (uint8_t*)terrain_staging_.cpu_ptr;
    std::memset(dst, (int)VoxMat::Air, cells);
    for (int iz = 0; iz < extent; ++iz)
        for (int ix = 0; ix < extent; ++ix) {
            const FloorColumn& fc = floor[(size_t)iz * extent + ix];
            for (int iy = 0; iy < fc.height && iy < hc; ++iy)
                dst[world.cell_index(ix, iy, iz)] = fc.material;
        }

    built_extent_ = extent;
    built_height_cells_ = hc;
    built_seed_ = seed;
    terrain_dirty_ = true;
}

void VoxelRenderer::encode_terrain_upload_if_dirty(void* command_buffer) {
    if (!terrain_dirty_ && !ripple_dirty_) return;
    id<MTLCommandBuffer> cb = (__bridge id<MTLCommandBuffer>)command_buffer;
    int extent = built_extent_;
    int hc     = built_height_cells_;

    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];

    if (terrain_dirty_) {
        [blit copyFromBuffer:(__bridge id<MTLBuffer>)terrain_staging_.handle
                sourceOffset:0
           sourceBytesPerRow:(NSUInteger)extent
         sourceBytesPerImage:(NSUInteger)extent * (NSUInteger)hc
                  sourceSize:MTLSizeMake((NSUInteger)extent, (NSUInteger)hc, (NSUInteger)extent)
                   toTexture:(__bridge id<MTLTexture>)terrain_grid_.handle
            destinationSlice:0
            destinationLevel:0
           destinationOrigin:MTLOriginMake(0, 0, 0)];
        terrain_dirty_ = false;
    }

    if (ripple_dirty_) {
        NSUInteger row_bytes   = (NSUInteger)extent * sizeof(float);
        NSUInteger image_bytes = (NSUInteger)extent * row_bytes;
        MTLSize    size        = MTLSizeMake((NSUInteger)extent, (NSUInteger)extent, 1);
        for (int i = 0; i < 3; ++i) {
            [blit copyFromBuffer:(__bridge id<MTLBuffer>)ripple_zero_staging_.handle
                    sourceOffset:0
               sourceBytesPerRow:row_bytes
             sourceBytesPerImage:image_bytes
                      sourceSize:size
                       toTexture:(__bridge id<MTLTexture>)ripple_[i].handle
                destinationSlice:0
                destinationLevel:0
               destinationOrigin:MTLOriginMake(0, 0, 0)];
        }
        ripple_dirty_ = false;
    }

    [blit endEncoding];
}

void VoxelRenderer::encode_ripple(void* compute_encoder, const Config& cfg, float dt,
                                   const RippleSplash* extra, int extra_count) {
    if (!ripple_[0].handle) return;
    id<MTLComputeCommandEncoder> ce = (__bridge id<MTLComputeCommandEncoder>)compute_encoder;
    int slot = ripple_phase_ % RING;
    int extent = cfg.voxel.grid_extent;

    // Debug rain: fixed seed so bench runs are reproducible frame-for-frame.
    RippleSplash* splashes = (RippleSplash*)splash_buf_[slot].cpu_ptr;
    int count = 0;
    if (cfg.ripple.rain_rate > 0.0f) {
        static thread_local std::mt19937 rng(0x5EAF00Du);   // fixed seed: bench determinism
        std::uniform_real_distribution<float> uni(0.0f, (float)extent);
        rain_accum_ = std::min(rain_accum_ + cfg.ripple.rain_rate * dt, (float)MAX_SPLASHES);
        while (rain_accum_ >= 1.0f && count < MAX_SPLASHES) {
            rain_accum_ -= 1.0f;
            splashes[count++] = { uni(rng), uni(rng), 2.0f, -0.35f };
        }
    }

    for (int i = 0; i < extra_count && count < MAX_SPLASHES; ++i)
        splashes[count++] = extra[i];

    RippleUniforms u{};
    u.grid_extent  = extent;
    // Fixed dt = 1/60 per frame (stability + bench determinism); the real
    // frame dt only drives the rain accumulator above.
    u.k            = ripple_k(cfg.ripple.wave_speed_mps, 1.0f / 60.0f, cfg.voxel.voxel_size_m);
    u.damping      = cfg.ripple.damping;
    u.splash_count = count;
    std::memcpy(ripple_uniforms_[slot].cpu_ptr, &u, sizeof(u));

    int prev = ripple_phase_ % 3, curr = (ripple_phase_ + 1) % 3, nxt = (ripple_phase_ + 2) % 3;
    [ce setComputePipelineState:(__bridge id<MTLComputePipelineState>)pso_ripple_];
    [ce setBuffer:(__bridge id<MTLBuffer>)ripple_uniforms_[slot].handle offset:0 atIndex:0];
    [ce setBuffer:(__bridge id<MTLBuffer>)splash_buf_[slot].handle offset:0 atIndex:1];
    [ce setTexture:(__bridge id<MTLTexture>)ripple_[prev].handle atIndex:0];
    [ce setTexture:(__bridge id<MTLTexture>)ripple_[curr].handle atIndex:1];
    [ce setTexture:(__bridge id<MTLTexture>)ripple_[nxt].handle atIndex:2];
    MTLSize tg = MTLSizeMake(16, 16, 1);
    NSUInteger groups = (NSUInteger)((extent + 15) / 16);
    [ce dispatchThreadgroups:MTLSizeMake(groups, groups, 1) threadsPerThreadgroup:tg];
    ripple_phase_++;
}

void VoxelRenderer::encode_world_fill(void* compute_encoder, const Config& cfg,
                                      Cascade* const* cascades, int cascade_count,
                                      int frame_index) {
    if (!world_grid_.handle) return;
    id<MTLComputeCommandEncoder> ce = (__bridge id<MTLComputeCommandEncoder>)compute_encoder;
    int slot = frame_index % RING;
    int extent = cfg.voxel.grid_extent;
    int n = std::min(cascade_count, MAX_CASCADES);

    WorldFillUniforms u{};
    u.grid_extent   = extent;
    u.height_cells  = cfg.voxel.height_cells;
    u.voxel_size_m  = cfg.voxel.voxel_size_m;
    u.height_step_m = cfg.voxel.height_step_m;
    u.base_depth_m  = cfg.voxel.base_depth_m;
    u.cascade_count = n;
    u.ripple_foam = cfg.ripple.foam; u._wpad1 = 0.0f;
    for (int i = 0; i < MAX_CASCADES; ++i)
        u.cascade_size[i] = i < n ? cfg.cascades[i].size_m : 0.0f;
    std::memcpy(fill_uniforms_[slot].cpu_ptr, &u, sizeof(u));

    [ce setComputePipelineState:(__bridge id<MTLComputePipelineState>)pso_fill_];
    [ce setBuffer:(__bridge id<MTLBuffer>)fill_uniforms_[slot].handle offset:0 atIndex:0];
    [ce setTexture:(__bridge id<MTLTexture>)terrain_grid_.handle atIndex:0];
    [ce setTexture:(__bridge id<MTLTexture>)world_grid_.handle atIndex:1];
    [ce setTexture:(__bridge id<MTLTexture>)surface_tex_.handle atIndex:2];
    for (int i = 0; i < n; ++i) {
        [ce setTexture:(__bridge id<MTLTexture>)cascades[i]->displacement_handle() atIndex:3 + i];
        [ce setTexture:(__bridge id<MTLTexture>)cascades[i]->normal_handle() atIndex:3 + MAX_CASCADES + i];
    }
    [ce setTexture:(__bridge id<MTLTexture>)ripple_[ripple_front_()].handle
           atIndex:3 + 2 * MAX_CASCADES];

    MTLSize tg = MTLSizeMake(16, 16, 1);
    NSUInteger groups = (NSUInteger)((extent + 15) / 16);
    MTLSize grid = MTLSizeMake(groups, groups, 1);
    [ce dispatchThreadgroups:grid threadsPerThreadgroup:tg];
}

void VoxelRenderer::ensure_march_target(const MetalContext& ctx, int drawable_w, int drawable_h,
                                        const Config& cfg) {
    float s = cfg.march.render_scale;
    int w = std::max(1, (int)(drawable_w * s));
    int h = std::max(1, (int)(drawable_h * s));
    if (march_target_.handle && w == target_w_ && h == target_h_) return;
    destroy_texture(march_target_);
    march_target_ = make_texture_2d(ctx, (uint32_t)w, (uint32_t)h,
                                    TexFormat::BGRA8Unorm_sRGB,
                                    /*storage_write=*/false, /*render_target=*/true);
    target_w_ = w;
    target_h_ = h;
}

void VoxelRenderer::encode_march(void* command_buffer, const OrbitCamera& cam, const Config& cfg,
                                 const SkyRenderer& sky, int frame_index) {
    if (!world_grid_.handle || !march_target_.handle) return;
    id<MTLCommandBuffer> cb = (__bridge id<MTLCommandBuffer>)command_buffer;
    int slot = frame_index % RING;

    MarchUniforms u{};
    glm::mat4 inv_vp = glm::inverse(cam.view_proj());
    std::memcpy(&u.inv_view_proj, &inv_vp[0][0], 64);
    u.camera_pos = (simd_float3){ cam.position().x, cam.position().y, cam.position().z };
    u._mpad0 = 0.0f;
    // sun_dir MUST be unit length (shader contract) — mirror SkyRenderer's
    // elevation/azimuth -> direction formula so sky and specular agree.
    float ce = std::cos(cfg.sky.sun_elevation_rad), se = std::sin(cfg.sky.sun_elevation_rad);
    float ca = std::cos(cfg.sky.sun_azimuth_rad),   sa = std::sin(cfg.sky.sun_azimuth_rad);
    simd_float3 sun = { ce * sa, se, ce * ca };
    u.sun_dir = simd_normalize(sun);
    u._mpad1 = 0.0f;
    u.sun_color = (simd_float3){ cfg.shading.sun_color.x, cfg.shading.sun_color.y, cfg.shading.sun_color.z };
    u.sun_shininess = cfg.shading.sun_shininess;
    u.deep_water_color = (simd_float3){ cfg.shading.deep_water_color.x, cfg.shading.deep_water_color.y, cfg.shading.deep_water_color.z };
    u.depth_fog_density = cfg.shading.depth_fog_density;
    u.extinction_rgb = (simd_float3){ cfg.shading.extinction_rgb.x, cfg.shading.extinction_rgb.y, cfg.shading.extinction_rgb.z };
    u.foam_threshold = cfg.shading.foam_threshold;
    u.sand_color = (simd_float3){ cfg.shading.sand_color.x, cfg.shading.sand_color.y, cfg.shading.sand_color.z };
    u.foam_strength = cfg.shading.foam_strength;
    u.rock_color = (simd_float3){ cfg.shading.rock_color.x, cfg.shading.rock_color.y, cfg.shading.rock_color.z };
    u.height_step_m = cfg.voxel.height_step_m;
    u.grid_extent = cfg.voxel.grid_extent;
    u.height_cells = cfg.voxel.height_cells;
    u.voxel_size_m = cfg.voxel.voxel_size_m;
    u.base_depth_m = cfg.voxel.base_depth_m;
    u.max_steps = cfg.march.max_steps;
    u.water_ior = cfg.shading.water_ior;
    u._mpad3 = u._mpad4 = 0.0f;
    u.boat_color = (simd_float3){ cfg.shading.boat_color.x, cfg.shading.boat_color.y, cfg.shading.boat_color.z };
    u._mpad5 = 0.0f;
    std::memcpy(march_uniforms_[slot].cpu_ptr, &u, sizeof(u));

    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = (__bridge id<MTLTexture>)march_target_.handle;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
    [enc setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)pso_march_];
    [enc setFragmentBuffer:(__bridge id<MTLBuffer>)march_uniforms_[slot].handle offset:0 atIndex:0];
    [enc setFragmentTexture:(__bridge id<MTLTexture>)world_grid_.handle atIndex:0];
    [enc setFragmentTexture:(__bridge id<MTLTexture>)surface_tex_.handle atIndex:1];
    [enc setFragmentTexture:(__bridge id<MTLTexture>)sky.cubemap_handle() atIndex:2];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [enc endEncoding];
}

void VoxelRenderer::encode_composite(void* render_encoder) {
    if (!march_target_.handle) return;
    id<MTLRenderCommandEncoder> enc = (__bridge id<MTLRenderCommandEncoder>)render_encoder;
    [enc setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)pso_composite_];
    [enc setDepthStencilState:(__bridge id<MTLDepthStencilState>)dss_off_];
    [enc setFragmentTexture:(__bridge id<MTLTexture>)march_target_.handle atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
}

void VoxelRenderer::encode_stamp(void* compute_encoder, const Config& cfg,
                                 const uint32_t* cells, int count, int frame_index) {
    if (!world_grid_.handle || count <= 0) return;
    id<MTLComputeCommandEncoder> ce = (__bridge id<MTLComputeCommandEncoder>)compute_encoder;
    int slot = frame_index % RING;
    int n = std::min(count, MAX_STAMP_CELLS);

    StampUniforms u{};
    u.grid_extent  = cfg.voxel.grid_extent;
    u.height_cells = cfg.voxel.height_cells;
    u.count        = n;
    u.material     = MAT_BOAT;
    std::memcpy(stamp_uniforms_[slot].cpu_ptr, &u, sizeof(u));
    std::memcpy(stamp_cells_[slot].cpu_ptr, cells, (size_t)n * sizeof(uint32_t));

    [ce setComputePipelineState:(__bridge id<MTLComputePipelineState>)pso_stamp_];
    [ce setBuffer:(__bridge id<MTLBuffer>)stamp_uniforms_[slot].handle offset:0 atIndex:0];
    [ce setBuffer:(__bridge id<MTLBuffer>)stamp_cells_[slot].handle offset:0 atIndex:1];
    [ce setTexture:(__bridge id<MTLTexture>)world_grid_.handle atIndex:0];
    [ce dispatchThreads:MTLSizeMake((NSUInteger)n, 1, 1)
        threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
}

void VoxelRenderer::encode_surface_readback(void* blit_encoder, const Config& cfg,
                                            int frame_index) {
    if (!surface_tex_.handle) return;
    id<MTLBlitCommandEncoder> blit = (__bridge id<MTLBlitCommandEncoder>)blit_encoder;
    int slot = frame_index % RING;
    int extent = cfg.voxel.grid_extent;
    [blit copyFromTexture:(__bridge id<MTLTexture>)surface_tex_.handle
              sourceSlice:0 sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(extent, extent, 1)
                 toBuffer:(__bridge id<MTLBuffer>)surface_readback_[slot].handle
        destinationOffset:0
   destinationBytesPerRow:(NSUInteger)extent * 2 * sizeof(float)
 destinationBytesPerImage:(NSUInteger)extent * extent * 2 * sizeof(float)];
}

float VoxelRenderer::water_height_at(float x, float z, const Config& cfg,
                                     int frame_index) const {
    int slot = frame_index % RING;   // written 3 frames ago: complete (in-flight <= 3)
    if (!surface_readback_[slot].cpu_ptr) return 0.0f;
    int extent = cfg.voxel.grid_extent;
    float half = 0.5f * extent * cfg.voxel.voxel_size_m;
    int ix = std::clamp((int)std::floor((x + half) / cfg.voxel.voxel_size_m), 0, extent - 1);
    int iz = std::clamp((int)std::floor((z + half) / cfg.voxel.voxel_size_m), 0, extent - 1);
    const float* buf = (const float*)surface_readback_[slot].cpu_ptr;
    return buf[((size_t)iz * extent + ix) * 2];   // RG32F: .r = water top_y
}

}
