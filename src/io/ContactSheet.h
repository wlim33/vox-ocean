#pragma once
#include <string>
#include <vector>
#include "io/Image.h"
namespace vox {
// Lay out per-view images left-to-right into one sheet. Each cell gets a top
// label bar drawn with a built-in 3x5 font (scaled by label_scale). `gap` px
// of background separate cells. Cells may differ in size; all are top-aligned
// under the label bar and the sheet height fits the tallest.
RgbImage make_contact_sheet(const std::vector<RgbImage>& cells,
                            const std::vector<std::string>& labels,
                            int gap = 8, int label_scale = 3);
}
