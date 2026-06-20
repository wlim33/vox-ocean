#import "voxel/DenseVoxelField.h"
#import "core/Config.h"
#import "gpu/MetalContext.h"
#import "gpu/PipelineCache.h"
#import "shader_types.h"
#include "entity/StampBudget.h"
#import <Metal/Metal.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace vox {

void DenseVoxelField::init(const MetalContext& ctx, PipelineCache& cache) {
    pso_apply_edits_ = cache.compute_pso(ctx, "apply_edits");
}

VoxelGridDesc DenseVoxelField::desc(const Config& c) const {
    return {c.voxel.grid_extent, c.voxel.height_cells, c.voxel.voxel_size_m,
            c.voxel.height_step_m, c.voxel.base_depth_m};
}

void DenseVoxelField::rebuild_if_dirty(const MetalContext& ctx, const Config& cfg) {
    int extent = cfg.voxel.grid_extent;
    int hc     = cfg.voxel.height_cells;
    int seed   = cfg.voxel.floor_seed;
    float bd   = cfg.voxel.base_depth_m;
    float hs   = cfg.voxel.height_step_m;
    if (extent == built_extent_ && hc == built_height_cells_ && seed == built_seed_
        && bd == built_base_depth_ && hs == built_height_step_
        && discrete_grid_.handle)
        return;

    destroy_texture(discrete_grid_);
    destroy_buffer(discrete_staging_);

    discrete_grid_ = make_texture_3d(ctx, (uint32_t)extent, (uint32_t)hc, (uint32_t)extent,
                                     TexFormat::R8Uint, /*storage_write=*/true);
    discrete_staging_ = make_buffer(ctx, (size_t)extent * hc * extent, true);

    built_extent_ = extent;
    built_height_cells_ = hc;
    built_seed_ = seed;
    built_base_depth_ = bd;
    built_height_step_ = hs;
}

void DenseVoxelField::ensure_capacity(const MetalContext& ctx, const Config& cfg) {
    // Cover entity stamps AND a sand-churn budget: a generous fraction of one grid
    // slab so a falling slab scatters via apply_edits rather than triggering a
    // full dense resync every frame. (Crossover to resync still fires above this.)
    int slab = cfg.voxel.grid_extent * cfg.voxel.grid_extent;     // one y-layer of cells
    int ecap = std::max(1, std::max(2 * max_stamp_cells(cfg), 4 * slab));
    if (ecap > built_edit_cap_ || !edit_cells_[0].handle) {
        for (int i = 0; i < RING; ++i) {
            destroy_buffer(edit_cells_[i]); destroy_buffer(edit_mats_[i]); destroy_buffer(apply_uniforms_[i]);
            edit_cells_[i]    = make_buffer(ctx, sizeof(uint32_t) * (size_t)ecap, true);
            edit_mats_[i]     = make_buffer(ctx, sizeof(uint8_t)  * (size_t)ecap, true);
            apply_uniforms_[i]= make_buffer(ctx, sizeof(ApplyEditsUniforms), true);
        }
        built_edit_cap_ = ecap;
    }
}

bool DenseVoxelField::discrete_needs_resync(const EditList& edits) const {
    return edits.resync || edits.count() > built_edit_cap_;
}

void DenseVoxelField::encode_apply_edits(void* compute_encoder, const Config& cfg,
                                         const EditList& edits, int frame) {
    if (edits.count() == 0) return;                 // nothing moved this frame
    int slot = frame % RING;
    int n = edits.count();
    std::memcpy(edit_cells_[slot].cpu_ptr, edits.idx.data(), (size_t)n * sizeof(uint32_t));
    std::memcpy(edit_mats_[slot].cpu_ptr,  edits.mat.data(), (size_t)n * sizeof(uint8_t));
    ApplyEditsUniforms u{ cfg.voxel.grid_extent, cfg.voxel.height_cells, n, 0 };
    std::memcpy(apply_uniforms_[slot].cpu_ptr, &u, sizeof(u));
    id<MTLComputeCommandEncoder> ce = (__bridge id<MTLComputeCommandEncoder>)compute_encoder;
    [ce setComputePipelineState:(__bridge id<MTLComputePipelineState>)pso_apply_edits_];
    [ce setBuffer:(__bridge id<MTLBuffer>)apply_uniforms_[slot].handle offset:0 atIndex:0];
    [ce setBuffer:(__bridge id<MTLBuffer>)edit_cells_[slot].handle     offset:0 atIndex:1];
    [ce setBuffer:(__bridge id<MTLBuffer>)edit_mats_[slot].handle      offset:0 atIndex:2];
    [ce setTexture:(__bridge id<MTLTexture>)discrete_grid_.handle atIndex:0];
    [ce dispatchThreads:MTLSizeMake((NSUInteger)n, 1, 1)
        threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
}

void DenseVoxelField::encode_discrete_resync(void* blit_encoder, const Config& cfg,
                                             const std::vector<uint8_t>& cells, int frame) {
    (void)frame;
    int extent = cfg.voxel.grid_extent, hc = cfg.voxel.height_cells;
    size_t want = (size_t)extent * hc * extent;
    if (!discrete_staging_.cpu_ptr || cells.size() < want) return;   // guarded
    std::memcpy(discrete_staging_.cpu_ptr, cells.data(), want);
    id<MTLBlitCommandEncoder> blit = (__bridge id<MTLBlitCommandEncoder>)blit_encoder;
    [blit copyFromBuffer:(__bridge id<MTLBuffer>)discrete_staging_.handle
            sourceOffset:0
       sourceBytesPerRow:(NSUInteger)extent
     sourceBytesPerImage:(NSUInteger)extent * (NSUInteger)hc
              sourceSize:MTLSizeMake((NSUInteger)extent, (NSUInteger)hc, (NSUInteger)extent)
               toTexture:(__bridge id<MTLTexture>)discrete_grid_.handle
        destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
}

}
