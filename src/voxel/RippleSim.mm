#import "voxel/RippleSim.h"
#import "core/Config.h"
#import "gpu/MetalContext.h"
#import "gpu/PipelineCache.h"
#import "voxel/Ripple.h"
#import "shader_types.h"
#import <Metal/Metal.h>
#include <algorithm>
#include <cstring>
#include <random>

namespace vox {

void RippleSim::init(const MetalContext& ctx, PipelineCache& cache) {
    pso_ripple_ = cache.compute_pso(ctx, "ripple_step");

    for (int i = 0; i < RING; ++i) {
        ripple_uniforms_[i] = make_buffer(ctx, sizeof(RippleUniforms), true);
        splash_buf_[i]      = make_buffer(ctx, sizeof(RippleSplash) * MAX_SPLASHES, true);
    }
}

void RippleSim::rebuild_if_dirty(const MetalContext& ctx, const Config& cfg) {
    int extent = cfg.voxel.grid_extent;
    int hc     = cfg.voxel.height_cells;
    int seed   = cfg.voxel.floor_seed;
    if (extent == built_extent_ && hc == built_height_cells_ && seed == built_seed_
        && ripple_[0].handle)
        return;

    destroy_buffer(ripple_zero_staging_);

    // Ripple ping-pong ring is sized by extent only; reset phase on rebuild.
    for (int i = 0; i < 3; ++i) {
        destroy_texture(ripple_[i]);
        ripple_[i] = make_texture_2d(ctx, (uint32_t)extent, (uint32_t)extent, TexFormat::R32F);
    }
    ripple_phase_ = 0;

    // Zero-fill staging buffer for the ripple ring; blitted into all three
    // textures by upload_zero_if_dirty to avoid undefined Metal
    // contents (NaN would propagate through the laplacian and never decay).
    size_t ripple_bytes = (size_t)extent * (size_t)extent * sizeof(float);
    ripple_zero_staging_ = make_buffer(ctx, ripple_bytes, true);
    std::memset(ripple_zero_staging_.cpu_ptr, 0, ripple_bytes);
    ripple_dirty_ = true;

    built_extent_       = extent;
    built_height_cells_ = hc;
    built_seed_         = seed;
}

void RippleSim::upload_zero_if_dirty(void* command_buffer) {
    if (!ripple_dirty_) return;
    id<MTLCommandBuffer> cb = (__bridge id<MTLCommandBuffer>)command_buffer;
    int extent = built_extent_;

    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];

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

    [blit endEncoding];
}

void RippleSim::encode(void* compute_encoder, const Config& cfg, float dt,
                       const RippleSplash* extra, int extra_count) {
    if (!ripple_[0].handle) return;
    id<MTLComputeCommandEncoder> ce = (__bridge id<MTLComputeCommandEncoder>)compute_encoder;
    int slot = ripple_phase_ % RING;
    int extent = cfg.voxel.grid_extent;

    RippleSplash* splashes = (RippleSplash*)splash_buf_[slot].cpu_ptr;
    int count = 0;
    // Entity splashes (boat wake) first: the debug rain must never starve
    // them out of the MAX_SPLASHES budget.
    for (int i = 0; i < extra_count && count < MAX_SPLASHES; ++i)
        splashes[count++] = extra[i];

    // Debug rain: fixed seed so bench runs are reproducible frame-for-frame.
    if (cfg.ripple.rain_rate > 0.0f) {
        static thread_local std::mt19937 rng(0x5EAF00Du);   // fixed seed: bench determinism
        std::uniform_real_distribution<float> uni(0.0f, (float)extent);
        rain_accum_ = std::min(rain_accum_ + cfg.ripple.rain_rate * dt, (float)MAX_SPLASHES);
        while (rain_accum_ >= 1.0f && count < MAX_SPLASHES) {
            rain_accum_ -= 1.0f;
            splashes[count++] = { uni(rng), uni(rng), 2.0f, -0.35f };
        }
    }

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

void* RippleSim::front_texture() const {
    return ripple_[front_index()].handle;
}

}
