#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "voxel/VoxelWorld.h"   // VoxMat, kNumMaterials
namespace vox {

// How the CA treats a material. SP1 models three; Liquid/Gas arrive in SP2.
enum class Phase : uint8_t { Empty = 0, Granular = 1, Solid = 2 };

struct MaterialProps {
    Phase phase;          // CA routing             — consumed: SP1
    float r, g, b;        // render palette column  — consumed: SP1
    float density;        // relative               — placeholder, consumer: SP2 (fluids)
    float flammability;   // 0..1                    — placeholder, consumer: SP3 (reactions)
    float melt_point;     // sentinel <0 = never     — placeholder, consumer: SP3 (reactions)
};

// Single source of truth, indexed by VoxMat. Density/flammability/melt_point are
// populated-but-unread until their consuming sub-project (see field comments).
inline constexpr std::array<MaterialProps, kNumMaterials> kMaterials = {{
    /* Air       */ { Phase::Empty,    0.00f, 0.00f, 0.00f,    0.0f, 0.0f, -1.0f },
    /* Water     */ { Phase::Solid,    0.10f, 0.30f, 0.45f, 1000.0f, 0.0f, -1.0f }, // not in material_ yet (SP2)
    /* Sand      */ { Phase::Solid,    0.76f, 0.70f, 0.50f, 1600.0f, 0.0f, -1.0f }, // static terrain
    /* Rock      */ { Phase::Solid,    0.35f, 0.33f, 0.30f, 2600.0f, 0.0f, -1.0f },
    /* Boat      */ { Phase::Solid,    0.45f, 0.30f, 0.18f,  700.0f, 0.6f, -1.0f },
    /* Kelp      */ { Phase::Solid,    0.13f, 0.32f, 0.15f,  500.0f, 0.4f, -1.0f },
    /* Fish      */ { Phase::Solid,    1.00f, 0.08f, 0.55f, 1000.0f, 0.0f, -1.0f },
    /* SandGrain */ { Phase::Granular, 0.88f, 0.80f, 0.55f, 1600.0f, 0.0f, -1.0f }, // dynamic sand (proof)
}};

static_assert(kMaterials.size() == (size_t)kNumMaterials, "registry size must match VoxMat count");

constexpr const MaterialProps& material_props(VoxMat m) {
    return kMaterials[static_cast<size_t>(m)];
}

// Flat RGB palette (3 floats per material) for GPU upload. The renderer copies
// this into MarchUniforms.palette; kept Metal-free so it is unit-tested.
inline void fill_palette(float* rgb /* [3 * kNumMaterials] */) {
    for (size_t i = 0; i < (size_t)kNumMaterials; ++i) {
        rgb[3*i+0] = kMaterials[i].r;
        rgb[3*i+1] = kMaterials[i].g;
        rgb[3*i+2] = kMaterials[i].b;
    }
}
}
