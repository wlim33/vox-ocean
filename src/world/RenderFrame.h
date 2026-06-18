#pragma once
#include "world/EditList.h"
namespace vox {

// CPU -> GPU boundary payload, built once per frame and consumed one-way. For
// now it carries the discrete-grid edit list; later migration steps add the
// water driver state and the camera view.
struct RenderFrame {
    EditList edits;
};
}
