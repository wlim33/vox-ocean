#pragma once
namespace vox {
// The axis a camera/projector looks ALONG. Shared by the GPU snapshot
// (AxisCamera) and the future CPU SliceView so their images are framed
// identically:
//   look along Y (top)   -> image is XZ  (width = X, height = Z)
//   look along Z (front) -> image is XY  (width = X, height = Y, +Y up)
//   look along X (side)  -> image is ZY  (width = Z, height = Y, +Y up)
enum class ViewAxis { X, Y, Z };
}
