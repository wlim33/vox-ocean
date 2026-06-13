#import "render/VoxelRenderer.h"
#import "render/SkyRenderer.h"
#import "ocean/Cascade.h"
#import "core/OrbitCamera.h"
#import "core/Config.h"
#import "gpu/MetalContext.h"
#import "gpu/PipelineCache.h"
#import "shader_types.h"
#include "entity/StampBudget.h"
#import <Metal/Metal.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/mat4x4.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace vox {

void VoxelRenderer::init(const MetalContext& ctx, PipelineCache& cache) {
    field_.init(ctx, cache);
    ripplesim_.init(ctx, cache);

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
        march_uniforms_[i] = make_buffer(ctx, sizeof(MarchUniforms), true);
    }
}

void VoxelRenderer::rebuild_if_dirty(const MetalContext& ctx, const Config& cfg) {
    field_.rebuild_if_dirty(ctx, cfg);
    ripplesim_.rebuild_if_dirty(ctx, cfg);
}

void VoxelRenderer::encode_terrain_upload_if_dirty(void* command_buffer) {
    field_.upload_terrain_if_dirty(command_buffer);
    ripplesim_.upload_zero_if_dirty(command_buffer);
}

void VoxelRenderer::encode_ripple(void* compute_encoder, const Config& cfg, float dt,
                                   const RippleSplash* extra, int extra_count) {
    ripplesim_.encode(compute_encoder, cfg, dt, extra, extra_count);
}

void VoxelRenderer::encode_world_fill(void* compute_encoder, const Config& cfg,
                                      Cascade* const* cascades, int cascade_count,
                                      int frame_index) {
    field_.encode_fill(compute_encoder, cfg, cascades, cascade_count,
                       ripplesim_.front_texture(), frame_index);
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
    if (!field_.world_grid_handle() || !march_target_.handle) return;
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
    u.kelp_color = (simd_float3){ cfg.shading.kelp_color.x, cfg.shading.kelp_color.y, cfg.shading.kelp_color.z };
    u._mpad6 = 0.0f;
    u.fish_color = (simd_float3){ cfg.shading.fish_color.x, cfg.shading.fish_color.y, cfg.shading.fish_color.z };
    u._mpad7 = 0.0f;
    std::memcpy(march_uniforms_[slot].cpu_ptr, &u, sizeof(u));

    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = (__bridge id<MTLTexture>)march_target_.handle;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
    [enc setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)pso_march_];
    [enc setFragmentBuffer:(__bridge id<MTLBuffer>)march_uniforms_[slot].handle offset:0 atIndex:0];
    [enc setFragmentTexture:(__bridge id<MTLTexture>)field_.world_grid_handle() atIndex:0];
    [enc setFragmentTexture:(__bridge id<MTLTexture>)field_.surface_handle() atIndex:1];
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

void VoxelRenderer::ensure_stamp_capacity(const MetalContext& ctx, const Config& cfg) {
    field_.ensure_capacity(ctx, cfg);
}

void VoxelRenderer::encode_stamp(void* compute_encoder, const Config& cfg,
                                 const uint32_t* cells, const uint8_t* mats,
                                 int count, int frame_index) {
    field_.encode_stamp_raw(compute_encoder, cfg, cells, mats, count, frame_index);
}

void VoxelRenderer::encode_surface_readback(void* blit_encoder, const Config& cfg,
                                            int frame_index) {
    field_.encode_readback(blit_encoder, cfg, frame_index);
}

float VoxelRenderer::water_height_at(float x, float z, const Config& cfg,
                                     int frame_index) const {
    return field_.height_at(x, z, cfg, frame_index);
}

}
