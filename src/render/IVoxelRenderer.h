#pragma once
namespace vox {
struct MetalContext; struct PipelineCache; struct Config; struct CameraView;
class SkyRenderer; class VoxelField;

class IVoxelRenderer {
public:
    virtual ~IVoxelRenderer() = default;
    virtual void init(const MetalContext&, PipelineCache&) = 0;
    virtual void resize(const MetalContext&, int w, int h, const Config&) = 0;
    // Pre-drawable passes (offscreen target / accel-structure build). May be empty.
    virtual void encode(void* command_buffer, const VoxelField&, const CameraView&,
                        const Config&, const SkyRenderer&, int frame) = 0;
    // Emit draws into the live drawable render pass (sky already drawn, depth attached).
    virtual void draw_into_drawable(void* render_encoder) = 0;
    virtual const char* name() const = 0;
};
}
