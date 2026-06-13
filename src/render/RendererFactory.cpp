#include "render/RendererFactory.h"
#include "render/RayMarchRenderer.h"
#include <cstdio>
namespace vox {
std::unique_ptr<IVoxelRenderer> make_renderer(const std::string& backend) {
    // Future backends register here, keyed by name.
    if (backend != "raymarch")
        fprintf(stderr, "[vox] unknown render.backend '%s', using raymarch\n", backend.c_str());
    return std::make_unique<RayMarchRenderer>();
}
}
