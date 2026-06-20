#pragma once
#include "voxel_grid.h"
namespace vox {
struct MetalContext; struct Config;

// Storage behind an interface: owns the material representation,
// exposes it to renderers (read) and producers (populate). Dense
// impl now; a sparse impl can satisfy the same contract later.
class VoxelField {
public:
    virtual ~VoxelField() = default;
    virtual VoxelGridDesc desc(const Config&) const = 0;
    virtual void rebuild_if_dirty(const MetalContext&, const Config&) = 0;
    virtual void ensure_capacity(const MetalContext&, const Config&) = 0;
    virtual void* discrete_grid_handle() const = 0;
};
}
