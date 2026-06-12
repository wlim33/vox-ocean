#pragma once
#include <cstdint>
namespace vox {
struct MetalContext;

enum class TexFormat { RG32F, RGBA16F, RGBA32F, RGBA8Unorm_sRGB, BGRA8Unorm_sRGB, R32F, R8Uint };

struct Texture {
    void*    handle = nullptr;
    uint32_t width = 0, height = 0;
    TexFormat format = TexFormat::RGBA16F;
    uint32_t depth = 1;
};

Texture make_texture_2d(const MetalContext& ctx, uint32_t w, uint32_t h, TexFormat f,
                        bool storage_write = true, bool render_target = false,
                        bool mipmapped = false);
Texture make_texture_cube(const MetalContext& ctx, uint32_t size, TexFormat f);
Texture make_texture_3d(const MetalContext& ctx, uint32_t w, uint32_t h, uint32_t d,
                        TexFormat f, bool storage_write = true);
void    destroy_texture(Texture& t);
}
