#pragma once
#include "world/EditList.h"
namespace vox {

// CPU -> GPU boundary payload, built once per frame by build_frame() and
// consumed one-way by consume_frame(). The camera is NOT here: it is a
// per-view render argument (the snapshot renders one frame from six cameras).
struct RenderFrame {
    EditList   edits;
};
}
