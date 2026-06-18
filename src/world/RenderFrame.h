#pragma once
#include "world/EditList.h"
#include "shader_types.h"   // RippleSplash
#include <vector>
namespace vox {

// Per-frame water-sim driver: absolute time + step for the FFT, plus this
// frame's interactive ripple impulses (boat wake). Shared across any views
// rendered from this frame.
struct WaterState {
    float time = 0.0f;
    float dt   = 0.0f;
    std::vector<RippleSplash> wake;
};

// CPU -> GPU boundary payload, built once per frame by build_frame() and
// consumed one-way by consume_frame(). The camera is NOT here: it is a
// per-view render argument (the snapshot renders one frame from six cameras).
struct RenderFrame {
    EditList   edits;
    WaterState water;
};
}
