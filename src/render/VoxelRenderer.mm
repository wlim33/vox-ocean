#import "render/VoxelRenderer.h"
#import "render/SkyRenderer.h"
#import "ocean/Cascade.h"
#import "core/OrbitCamera.h"
#import "core/Config.h"
#import "gpu/MetalContext.h"
#import "gpu/PipelineCache.h"
#import "shader_types.h"
#import <Metal/Metal.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace vox {

struct CubeVert { float px, py, pz, nx, ny, nz; };

static void push_face(std::vector<CubeVert>& v, std::vector<uint16_t>& idx,
                      float ox, float oy, float oz,
                      float e1x, float e1y, float e1z,
                      float e2x, float e2y, float e2z,
                      float nx, float ny, float nz) {
    uint16_t base = (uint16_t)v.size();
    v.push_back({ox,         oy,         oz,         nx, ny, nz});
    v.push_back({ox+e1x,     oy+e1y,     oz+e1z,     nx, ny, nz});
    v.push_back({ox+e1x+e2x, oy+e1y+e2y, oz+e1z+e2z, nx, ny, nz});
    v.push_back({ox+e2x,     oy+e2y,     oz+e2z,     nx, ny, nz});
    for (uint16_t i : {(uint16_t)0,(uint16_t)1,(uint16_t)2,(uint16_t)0,(uint16_t)2,(uint16_t)3})
        idx.push_back(base + i);
}

void VoxelRenderer::init(const MetalContext& ctx, PipelineCache& cache) {
    // Unit column [0,1]^3, 5 faces (no bottom: the orbit camera never sees it),
    // 4 verts + 6 indices per face = 30 indices. Per-face normals.
    std::vector<CubeVert> verts;
    std::vector<uint16_t> indices;
    // top (+y), -x, +x, -z, +z
    push_face(verts, indices, 0,1,0,  1,0,0,  0,0,1,   0, 1, 0);
    push_face(verts, indices, 0,0,0,  0,0,1,  0,1,0,  -1, 0, 0);
    push_face(verts, indices, 1,0,1,  0,0,-1, 0,1,0,   1, 0, 0);
    push_face(verts, indices, 1,0,0,  -1,0,0, 0,1,0,   0, 0,-1);
    push_face(verts, indices, 0,0,1,  1,0,0,  0,1,0,   0, 0, 1);
    index_count_ = (int)indices.size();

    cube_vbo_ = make_buffer(ctx, verts.size() * sizeof(CubeVert), true);
    cube_ibo_ = make_buffer(ctx, indices.size() * sizeof(uint16_t), true);
    std::memcpy(cube_vbo_.cpu_ptr, verts.data(), verts.size() * sizeof(CubeVert));
    std::memcpy(cube_ibo_.cpu_ptr, indices.data(), indices.size() * sizeof(uint16_t));

    // Render PSO: stage_in pos(0)/normal(1), color BGRA8Unorm_sRGB, depth Depth32Float.
    RenderPSODesc d;
    d.vertex_fn = "voxel_vs";
    d.fragment_fn = "voxel_fs";
    d.depth_pixel_format = (unsigned)MTLPixelFormatDepth32Float;
    d.attrs[0] = { (unsigned)MTLVertexFormatFloat3, 0 };
    d.attrs[1] = { (unsigned)MTLVertexFormatFloat3, 12 };
    d.vertex_stride = (unsigned)sizeof(CubeVert);   // 24
    pso_draw_ = cache.render_pso(ctx, d);

    pso_voxelize_ = cache.compute_pso(ctx, "voxelize");

    id<MTLDevice> dev = (__bridge id<MTLDevice>)ctx.device;
    MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
    dd.depthCompareFunction = MTLCompareFunctionLess;
    dd.depthWriteEnabled = YES;
    dss_ = (__bridge_retained void*)[dev newDepthStencilStateWithDescriptor:dd];

    for (int i = 0; i < RING; ++i) {
        vox_uniforms_[i]  = make_buffer(ctx, sizeof(VoxelizeUniforms), true);
        draw_uniforms_[i] = make_buffer(ctx, sizeof(VoxelSurfaceUniforms), true);
        cam_buf_[i]       = make_buffer(ctx, sizeof(CameraUniforms), true);
    }
}

void VoxelRenderer::rebuild_if_dirty(const MetalContext& ctx, const Config& cfg) {
    int extent = cfg.voxel.grid_extent;
    if (extent == instance_extent_ && instances_.handle) return;
    destroy_buffer(instances_);
    size_t bytes = (size_t)extent * (size_t)extent * sizeof(VoxelInstance);
    // storageModePrivate: the GPU both writes (voxelize) and reads (draw) this.
    instances_ = make_buffer(ctx, bytes, false);
    instance_extent_ = extent;
}

void VoxelRenderer::encode_voxelize(void* compute_encoder, const Config& cfg,
                                    Cascade* const* cascades, int cascade_count) {
    if (!instances_.handle) return;
    id<MTLComputeCommandEncoder> ce = (__bridge id<MTLComputeCommandEncoder>)compute_encoder;
    int slot = frame_index_ % RING;
    int extent = cfg.voxel.grid_extent;
    int n = std::min(cascade_count, MAX_CASCADES);

    VoxelizeUniforms u{};
    u.grid_extent  = extent;
    u.voxel_size_m = cfg.voxel.voxel_size_m;
    u.height_step_m = cfg.voxel.height_step_m;
    u.base_depth_m = cfg.voxel.base_depth_m;
    u.cascade_count = n;
    u._vpad0 = u._vpad1 = u._vpad2 = 0.0f;
    for (int i = 0; i < MAX_CASCADES; ++i)
        u.cascade_size[i] = i < n ? cfg.cascades[i].size_m : 0.0f;
    std::memcpy(vox_uniforms_[slot].cpu_ptr, &u, sizeof(u));

    [ce setComputePipelineState:(__bridge id<MTLComputePipelineState>)pso_voxelize_];
    [ce setBuffer:(__bridge id<MTLBuffer>)vox_uniforms_[slot].handle offset:0 atIndex:0];
    [ce setBuffer:(__bridge id<MTLBuffer>)instances_.handle offset:0 atIndex:1];
    for (int i = 0; i < n; ++i) {
        [ce setTexture:(__bridge id<MTLTexture>)cascades[i]->displacement_handle() atIndex:i];
        [ce setTexture:(__bridge id<MTLTexture>)cascades[i]->normal_handle() atIndex:MAX_CASCADES + i];
    }

    MTLSize tg = MTLSizeMake(16, 16, 1);
    NSUInteger groups = (NSUInteger)((extent + 15) / 16);
    MTLSize grid = MTLSizeMake(groups, groups, 1);
    [ce dispatchThreadgroups:grid threadsPerThreadgroup:tg];

    frame_index_++;
}

void VoxelRenderer::encode_draw(void* render_encoder, const OrbitCamera& cam, const Config& cfg,
                                const SkyRenderer& sky, int frame_index) {
    if (!instances_.handle) return;
    id<MTLRenderCommandEncoder> enc = (__bridge id<MTLRenderCommandEncoder>)render_encoder;
    int slot = frame_index % RING;
    int extent = cfg.voxel.grid_extent;

    CameraUniforms cu;
    auto v = cam.view(); auto p = cam.proj(); auto vp = cam.view_proj();
    std::memcpy(&cu.view, &v[0][0], 64);
    std::memcpy(&cu.proj, &p[0][0], 64);
    std::memcpy(&cu.view_proj, &vp[0][0], 64);
    cu.position = (simd_float3){ cam.position().x, cam.position().y, cam.position().z };
    cu._pad = 0.0f;
    std::memcpy(cam_buf_[slot].cpu_ptr, &cu, sizeof(cu));

    VoxelSurfaceUniforms su{};
    su.grid_extent = extent;
    su.voxel_size_m = cfg.voxel.voxel_size_m;
    su.base_depth_m = cfg.voxel.base_depth_m;
    su._spad_a = 0.0f;
    // sun_dir MUST be unit length (shader contract) — mirror SkyRenderer's
    // elevation/azimuth -> direction formula so sky and specular agree.
    float ce = std::cos(cfg.sky.sun_elevation_rad), se = std::sin(cfg.sky.sun_elevation_rad);
    float ca = std::cos(cfg.sky.sun_azimuth_rad),   sa = std::sin(cfg.sky.sun_azimuth_rad);
    simd_float3 sun = { ce * sa, se, ce * ca };
    su.sun_dir = simd_normalize(sun);
    su.sun_color = (simd_float3){ cfg.shading.sun_color.x, cfg.shading.sun_color.y, cfg.shading.sun_color.z };
    su.sun_shininess = cfg.shading.sun_shininess;
    su.deep_water_color = (simd_float3){ cfg.shading.deep_water_color.x, cfg.shading.deep_water_color.y, cfg.shading.deep_water_color.z };
    su.depth_fog_density = cfg.shading.depth_fog_density;
    su.extinction_rgb = (simd_float3){ cfg.shading.extinction_rgb.x, cfg.shading.extinction_rgb.y, cfg.shading.extinction_rgb.z };
    su.foam_threshold = cfg.shading.foam_threshold;
    su.foam_strength = cfg.shading.foam_strength;
    su.height_step_m = cfg.voxel.height_step_m;
    std::memcpy(draw_uniforms_[slot].cpu_ptr, &su, sizeof(su));

    [enc setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)pso_draw_];
    [enc setDepthStencilState:(__bridge id<MTLDepthStencilState>)dss_];
    [enc setVertexBuffer:(__bridge id<MTLBuffer>)cube_vbo_.handle offset:0 atIndex:0];
    [enc setVertexBuffer:(__bridge id<MTLBuffer>)cam_buf_[slot].handle offset:0 atIndex:1];
    [enc setVertexBuffer:(__bridge id<MTLBuffer>)draw_uniforms_[slot].handle offset:0 atIndex:2];
    [enc setVertexBuffer:(__bridge id<MTLBuffer>)instances_.handle offset:0 atIndex:3];
    [enc setFragmentBuffer:(__bridge id<MTLBuffer>)cam_buf_[slot].handle offset:0 atIndex:1];
    [enc setFragmentBuffer:(__bridge id<MTLBuffer>)draw_uniforms_[slot].handle offset:0 atIndex:2];
    [enc setFragmentTexture:(__bridge id<MTLTexture>)sky.cubemap_handle() atIndex:0];

    [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                    indexCount:index_count_
                     indexType:MTLIndexTypeUInt16
                   indexBuffer:(__bridge id<MTLBuffer>)cube_ibo_.handle
             indexBufferOffset:0
                 instanceCount:(NSUInteger)extent * (NSUInteger)extent];
}

}
