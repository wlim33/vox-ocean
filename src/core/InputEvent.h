#pragma once
#include <cstdint>
namespace vox {

// Fixed underlying type: this struct crosses the Swift C++-interop boundary
// by value, so its layout must not be implementation-defined.
enum class InputKind : int32_t { MouseMove, MouseDown, MouseUp, Scroll, KeyDown, KeyUp, Resize, Pick, Draw };

struct InputEvent {
    InputKind kind = InputKind::MouseMove;
    float x = 0, y = 0;     // for MouseMove (delta in pixels) / cursor pos
    float scroll = 0;        // for Scroll
    int   button = 0;        // 0=left, 1=right, 2=middle
    int   key = 0;           // platform key code
    int   width = 0, height = 0; // for Resize
};
}
