#pragma once
#include <string>
#include <unordered_map>
namespace vox {
struct MetalContext;

// Up to 2 interleaved float vertex attributes from buffer 0 (covers the
// fullscreen-triangle no-attr case and the voxel cube's pos+normal layout).
struct VertexAttr {
    unsigned format = 0;   // MTLVertexFormat (e.g. Float3 = 30); 0 = unused
    unsigned offset = 0;
};

struct RenderPSODesc {
    std::string vertex_fn;
    std::string fragment_fn;
    unsigned    color_pixel_format = 81; // MTLPixelFormatBGRA8Unorm_sRGB (= 81)
    unsigned    depth_pixel_format = 0;  // 0 = none; MTLPixelFormatDepth32Float = 252
    bool        blending = false;
    VertexAttr  attrs[2]{};
    unsigned    vertex_stride = 0;       // 0 = no vertex descriptor
};

struct PipelineCache {
    void* render_pso(const MetalContext& ctx, const RenderPSODesc& d);
    void* compute_pso(const MetalContext& ctx, const std::string& fn);

    std::unordered_map<std::string, void*> render_cache_;
    std::unordered_map<std::string, void*> compute_cache_;
};

}
