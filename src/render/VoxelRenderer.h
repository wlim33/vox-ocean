#pragma once
#include "gpu/Buffer.h"
#include "gpu/Texture.h"
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
    void encode_ripple(void* compute_encoder, const Config& cfg, float dt);
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
    Texture terrain_grid_{}, world_grid_{}, surface_tex_{}, march_target_{};
    // staged terrain lives until the next rebuild; the blit (encode_terrain_upload_if_dirty) may run a frame later than generation
    Buffer  terrain_staging_{};
    Buffer  ripple_zero_staging_{};   // zeroed float buffer blitted into all three ripple_ textures after rebuild
    Buffer  fill_uniforms_[RING]{}, march_uniforms_[RING]{};
    Texture ripple_[3]{};            // wave-equation ping-pong ring (prev/cur/next)
    int     ripple_phase_ = 0;
    Buffer  ripple_uniforms_[RING]{}, splash_buf_[RING]{};
    float   rain_accum_ = 0.0f;
    void*   pso_ripple_ = nullptr;
    int     ripple_front_() const { return (ripple_phase_ + 1) % 3; }   // freshly written field
    int     built_extent_ = 0, built_height_cells_ = 0, built_seed_ = 0;
    int     target_w_ = 0, target_h_ = 0;
    bool    terrain_dirty_ = false;
    bool    ripple_dirty_  = false;
    void*   pso_fill_ = nullptr;
    void*   pso_march_ = nullptr;
    void*   pso_composite_ = nullptr;
    void*   dss_off_ = nullptr;   // depth Always, write OFF (drawable pass has a depth buffer)
};
}
