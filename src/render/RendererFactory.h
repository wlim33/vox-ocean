#pragma once
#include <memory>
#include <string>
namespace vox {
class IVoxelRenderer;
std::unique_ptr<IVoxelRenderer> make_renderer(const std::string& backend);
}
