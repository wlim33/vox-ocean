#import "voxel/DenseVoxelField.h"
#import "ocean/Cascade.h"
#import "core/Config.h"
#import "gpu/MetalContext.h"
#import "gpu/PipelineCache.h"
#import "voxel/VoxelWorld.h"
#import "voxel/FloorGen.h"
#import "shader_types.h"
#include "entity/StampBudget.h"
#import <Metal/Metal.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace vox {

void DenseVoxelField::init(const MetalContext& ctx, PipelineCache& cache) {
    pso_fill_  = cache.compute_pso(ctx, "world_fill");
    pso_stamp_ = cache.compute_pso(ctx, "stamp_cells");

    for (int i = 0; i < RING; ++i) {
        fill_uniforms_[i]  = make_buffer(ctx, sizeof(WorldFillUniforms), true);
        stamp_uniforms_[i] = make_buffer(ctx, sizeof(StampUniforms), true);
    }
}

VoxelGridDesc DenseVoxelField::desc(const Config& c) const {
    return {c.voxel.grid_extent, c.voxel.height_cells, c.voxel.voxel_size_m,
            c.voxel.height_step_m, c.voxel.base_depth_m};
}

void DenseVoxelField::rebuild_if_dirty(const MetalContext& ctx, const Config& cfg) {
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

    // terrain_grid_ is blit-uploaded once then read-only; world_grid_ is
    // compute-written each frame; surface_tex_ carries per-column water state.
    terrain_grid_ = make_texture_3d(ctx, (uint32_t)extent, (uint32_t)hc, (uint32_t)extent,
                                    TexFormat::R8Uint, /*storage_write=*/false);
    world_grid_   = make_texture_3d(ctx, (uint32_t)extent, (uint32_t)hc, (uint32_t)extent,
                                    TexFormat::R8Uint, /*storage_write=*/true);
    surface_tex_  = make_texture_2d(ctx, (uint32_t)extent, (uint32_t)extent,
                                    TexFormat::RG32F);

    // Surface readback ring: CPU-readable buffers for height_at.
    // Zeroed so the boat reads height 0 until real data lands.
    for (int i = 0; i < RING; ++i) {
        destroy_buffer(surface_readback_[i]);
        surface_readback_[i] = make_buffer(ctx, (size_t)extent * extent * 2 * sizeof(float), true);
        std::memset(surface_readback_[i].cpu_ptr, 0, surface_readback_[i].size);
    }

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

void DenseVoxelField::upload_terrain_if_dirty(void* command_buffer) {
    if (!terrain_dirty_) return;
    id<MTLCommandBuffer> cb = (__bridge id<MTLCommandBuffer>)command_buffer;
    int extent = built_extent_;
    int hc     = built_height_cells_;

    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];

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

    [blit endEncoding];

    // Reclaim the staging buffer (2.25 MiB at default extent); it is
    // regenerated on the next rebuild_if_dirty.
    destroy_buffer(terrain_staging_);
}

void DenseVoxelField::ensure_capacity(const MetalContext& ctx, const Config& cfg) {
    int cap = std::max(1, max_stamp_cells(cfg));
    if (cap <= built_stamp_cap_ && stamp_cells_[0].handle) return;   // grow-only
    for (int i = 0; i < RING; ++i) {
        destroy_buffer(stamp_cells_[i]);
        destroy_buffer(stamp_mats_[i]);
        stamp_cells_[i] = make_buffer(ctx, sizeof(uint32_t) * (size_t)cap, true);
        stamp_mats_[i]  = make_buffer(ctx, sizeof(uint8_t)  * (size_t)cap, true);
    }
    built_stamp_cap_ = cap;
}

void DenseVoxelField::encode_fill(void* compute_encoder, const Config& cfg,
                                  Cascade* const* cascades, int cascade_count,
                                  void* ripple_front_tex, int frame_index) {
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
    [ce setTexture:(__bridge id<MTLTexture>)ripple_front_tex
           atIndex:3 + 2 * MAX_CASCADES];

    MTLSize tg = MTLSizeMake(16, 16, 1);
    NSUInteger groups = (NSUInteger)((extent + 15) / 16);
    MTLSize grid = MTLSizeMake(groups, groups, 1);
    [ce dispatchThreadgroups:grid threadsPerThreadgroup:tg];
}

void DenseVoxelField::encode_stamp(void* compute_encoder, const Config& cfg,
                                   const StampList& stamp, int frame_index) {
    int count = stamp.count();
    if (!world_grid_.handle || count <= 0 || built_stamp_cap_ <= 0) return;
    id<MTLComputeCommandEncoder> ce = (__bridge id<MTLComputeCommandEncoder>)compute_encoder;
    int slot = frame_index % RING;
    int n = std::min(count, built_stamp_cap_);
    if (count > built_stamp_cap_) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            fprintf(stderr, "[vox] stamp truncated: %d cells exceed capacity %d\n", count, built_stamp_cap_);
        }
    }

    StampUniforms u{};
    u.grid_extent  = cfg.voxel.grid_extent;
    u.height_cells = cfg.voxel.height_cells;
    u.count        = n;
    std::memcpy(stamp_uniforms_[slot].cpu_ptr, &u, sizeof(u));
    std::memcpy(stamp_cells_[slot].cpu_ptr, stamp.idx.data(), (size_t)n * sizeof(uint32_t));
    std::memcpy(stamp_mats_[slot].cpu_ptr,  stamp.mat.data(), (size_t)n * sizeof(uint8_t));

    [ce setComputePipelineState:(__bridge id<MTLComputePipelineState>)pso_stamp_];
    [ce setBuffer:(__bridge id<MTLBuffer>)stamp_uniforms_[slot].handle offset:0 atIndex:0];
    [ce setBuffer:(__bridge id<MTLBuffer>)stamp_cells_[slot].handle offset:0 atIndex:1];
    [ce setBuffer:(__bridge id<MTLBuffer>)stamp_mats_[slot].handle offset:0 atIndex:2];
    [ce setTexture:(__bridge id<MTLTexture>)world_grid_.handle atIndex:0];
    [ce dispatchThreads:MTLSizeMake((NSUInteger)n, 1, 1)
        threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
}

void DenseVoxelField::encode_readback(void* blit_encoder, const Config& cfg,
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

float DenseVoxelField::height_at(float x, float z, const Config& cfg,
                                 int frame_index) const {
    int slot = frame_index % RING;   // written 3 frames ago: complete (in-flight <= 3)
    if (!surface_readback_[slot].cpu_ptr) return 0.0f;
    int extent = cfg.voxel.grid_extent;
    const float* buf = (const float*)surface_readback_[slot].cpu_ptr;
    VgCol c = vg_column_at(desc(cfg), x, z);
    return buf[((size_t)c.iz * extent + c.ix) * 2];   // RG32F: .r = water top_y
}

}
