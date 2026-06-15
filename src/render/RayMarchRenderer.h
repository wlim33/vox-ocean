#pragma once
#include "render/IVoxelRenderer.h"
#include "gpu/Buffer.h"
#include "gpu/Texture.h"
namespace vox {
class RayMarchRenderer : public IVoxelRenderer {
public:
    static constexpr int RING = 3;
    void init(const MetalContext&, PipelineCache&) override;
    void resize(const MetalContext&, int w, int h, const Config&) override;
    void encode(void* command_buffer, const VoxelField&, const CameraView&,
                const Config&, const SkyRenderer&, int frame) override;
    void draw_into_drawable(void* render_encoder) override;
    const char* name() const override { return "raymarch"; }
private:
    Texture march_target_{};
    Buffer  march_uniforms_[RING]{};
    int     target_w_ = 0, target_h_ = 0;
    void*   pso_march_ = nullptr;
    void*   pso_composite_ = nullptr;
    void*   dss_off_ = nullptr;
};
}
