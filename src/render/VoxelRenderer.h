#pragma once
#include "gpu/Buffer.h"
#include "gpu/Texture.h"
#include "shader_types.h"
#include "voxel/DenseVoxelField.h"
#include <cstdint>
namespace vox {
struct MetalContext;
struct PipelineCache;
class  OrbitCamera;
struct Config;
class  Cascade;
class  SkyRenderer;

// Owns the voxel world: static terrain, the per-frame material-grid build
// (compute), and the ray-marched draw (offscreen march pass + composite into
// the drawable pass).
class VoxelRenderer {
public:
    void init(const MetalContext& ctx, PipelineCache& cache);
    // Recreates grid textures + regenerates terrain when extent / height_cells
    // / floor_seed change.
    void rebuild_if_dirty(const MetalContext& ctx, const Config& cfg);
    // Stages freshly generated terrain and zeroes the ripple ring into their
    // private textures. Creates its own blit encoder; call BEFORE
    // encode_world_fill in the frame.
    void encode_terrain_upload_if_dirty(void* command_buffer);
    // Advances the ripple sim one fixed 1/60s step and injects this frame's
    // splashes (debug rain). Call before encode_world_fill.
    // Extra splashes (e.g. the boat's wake) are appended to this frame's
    // rain; both go through the same splash buffer.
    void encode_ripple(void* compute_encoder, const Config& cfg, float dt,
                       const RippleSplash* extra = nullptr, int extra_count = 0);
    // Grow the stamp ring buffers to hold max_stamp_cells(cfg). Grow-only;
    // call each frame before encode_stamp (mirrors ensure_march_target).
    void ensure_stamp_capacity(const MetalContext& ctx, const Config& cfg);
    // Stamp entity cells+materials into the world grid. Call AFTER
    // encode_world_fill so entities overwrite water.
    void encode_stamp(void* compute_encoder, const Config& cfg,
                      const uint32_t* cells, const uint8_t* mats, int count,
                      int frame_index);
    // Copies surface_tex into a CPU-readable ring. Call in the blit phase.
    void encode_surface_readback(void* blit_encoder, const Config& cfg,
                                 int frame_index);
    // Water surface height at world (x,z), from the readback slot written 3
    // frames ago (guaranteed complete by the in-flight pacing). Returns 0
    // until the first readback lands.
    float water_height_at(float x, float z, const Config& cfg, int frame_index) const;
    void encode_world_fill(void* compute_encoder, const Config& cfg,
                           Cascade* const* cascades, int cascade_count, int frame_index);
    // (Re)sizes the offscreen march target to drawable * march.render_scale.
    void ensure_march_target(const MetalContext& ctx, int drawable_w, int drawable_h,
                             const Config& cfg);
    // Ray-march into the offscreen target: own render pass on the command buffer.
    void encode_march(void* command_buffer, const OrbitCamera& cam, const Config& cfg,
                      const SkyRenderer& sky, int frame_index);
    // Blend the march target over the sky in the drawable pass.
    void encode_composite(void* render_encoder);
    // Uniform ring paced by drawable acquisition + the engine's in-flight
    // semaphore, exactly as in v1 (see git history for the full contract).
    static constexpr int RING = 3;
private:
    DenseVoxelField field_;          // owns grid storage + fill/stamp/readback producers
    Texture march_target_{};
    Buffer  ripple_zero_staging_{};   // zeroed float buffer blitted into all three ripple_ textures after rebuild
    Buffer  march_uniforms_[RING]{};
    Texture ripple_[3]{};            // wave-equation ping-pong ring (prev/cur/next)
    int     ripple_phase_ = 0;
    Buffer  ripple_uniforms_[RING]{}, splash_buf_[RING]{};
    float   rain_accum_ = 0.0f;
    void*   pso_ripple_ = nullptr;
    // Ripple ring rebuild tracking — mirrors the field's storage rebuild so the
    // ripple sim resets (zero + phase 0) on the same extent/hc/seed changes.
    int     ripple_built_extent_ = 0, ripple_built_hc_ = 0, ripple_built_seed_ = 0;
    int     ripple_front_() const { return (ripple_phase_ + 1) % 3; }   // freshly written field
    int     target_w_ = 0, target_h_ = 0;
    bool    ripple_dirty_  = false;
    void*   pso_march_ = nullptr;
    void*   pso_composite_ = nullptr;
    void*   dss_off_ = nullptr;   // depth Always, write OFF (drawable pass has a depth buffer)
};
}
