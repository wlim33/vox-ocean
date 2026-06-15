#pragma once
#include "gpu/Texture.h"
#include <cstdint>
namespace vox {
struct MetalContext;
struct PipelineCache;
struct CameraView;
struct Config;

class SkyRenderer {
public:
    void init(const MetalContext& ctx, PipelineCache& cache);
    void encode_full_screen(void* encoder, const CameraView& cam, const Config& cfg);
    void bake_cubemap_if_dirty(const MetalContext& ctx, void* command_buffer, const Config& cfg);
    void* cubemap_handle() const { return cubemap_.handle; }

private:
    void create_cubemap(const MetalContext& ctx, int size);
    void* pso_ = nullptr;
    void* pso_cube_ = nullptr;
    void* dss_off_ = nullptr;   // depth write OFF, compare Always (sky never occludes)
    Texture cubemap_{};
    uint64_t last_config_hash_ = 0;
};
}
