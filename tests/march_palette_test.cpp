#include <gtest/gtest.h>
#include "shader_types.h"
#include "voxel/MaterialRegistry.h"

using namespace vox;

// Mirror of the copy RayMarchRenderer does, exercised without Metal.
static void upload_palette(MarchUniforms& u) {
    float rgb[3 * kNumMaterials];
    fill_palette(rgb);
    for (int i = 0; i < NUM_MATERIALS; ++i)
        u.palette[i] = (simd_float3){ rgb[3*i+0], rgb[3*i+1], rgb[3*i+2] };
}

TEST(MarchPalette, PaletteIndexMatchesTable) {
    MarchUniforms u{};
    upload_palette(u);
    EXPECT_FLOAT_EQ(u.palette[MAT_ROCK].x,      kMaterials[(size_t)VoxMat::Rock].r);
    EXPECT_FLOAT_EQ(u.palette[MAT_SANDGRAIN].y, kMaterials[(size_t)VoxMat::SandGrain].g);
    EXPECT_FLOAT_EQ(u.palette[MAT_FISH].z,      kMaterials[(size_t)VoxMat::Fish].b);
}

TEST(MarchPalette, BubblePaletteColumn) {
    MarchUniforms u{};
    upload_palette(u);
    EXPECT_FLOAT_EQ(u.palette[MAT_BUBBLE].x, kMaterials[(size_t)VoxMat::Bubble].r);
    EXPECT_FLOAT_EQ(u.palette[MAT_BUBBLE].z, kMaterials[(size_t)VoxMat::Bubble].b);
}
