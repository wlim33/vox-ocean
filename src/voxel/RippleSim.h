#pragma once
#include "gpu/Buffer.h"
#include "gpu/Texture.h"
#include "shader_types.h"
namespace vox {
struct MetalContext; struct PipelineCache; struct Config;

// Damped 2D wave-equation surface ripple (ping-pong), an input to the field's
// fill pass. Resets (re-zeros) on the same extent/height_cells/seed change as
// the grid rebuild, to preserve original behavior exactly.
class RippleSim {
public:
    static constexpr int RING = 3;
    void init(const MetalContext&, PipelineCache&);
    void rebuild_if_dirty(const MetalContext&, const Config&);   // (re)creates the ping-pong ring
    void upload_zero_if_dirty(void* command_buffer);             // zero-fill after rebuild
    void encode(void* compute_encoder, const Config&, float dt,
                const RippleSplash* extra = nullptr, int extra_count = 0);
    void* front_texture() const;   // freshly written field, consumed by encode_fill
private:
    Texture ripple_[3]{};
    Buffer  ripple_zero_staging_{};
    Buffer  ripple_uniforms_[RING]{}, splash_buf_[RING]{};
    int     ripple_phase_ = 0;
    float   rain_accum_ = 0.0f;
    bool    ripple_dirty_ = false;
    int     built_extent_ = 0, built_height_cells_ = 0, built_seed_ = 0;
    void*   pso_ripple_ = nullptr;
    int     front_index() const { return (ripple_phase_ + 1) % 3; }
};
}
