#pragma once
#include "voxel_grid.h"
#include "entity/StampBudget.h"
namespace vox {
struct MetalContext; struct Config; class Cascade;

// Storage behind an interface: owns the material representation and the 2D
// surface, exposes them to renderers (read) and producers (populate). Dense
// impl now; a sparse impl can satisfy the same contract later.
class VoxelField {
public:
    virtual ~VoxelField() = default;
    virtual VoxelGridDesc desc(const Config&) const = 0;
    virtual void rebuild_if_dirty(const MetalContext&, const Config&) = 0;
    virtual void upload_terrain_if_dirty(void* command_buffer) = 0;
    virtual void ensure_capacity(const MetalContext&, const Config&) = 0;
    virtual void encode_fill(void* compute_encoder, const Config&, Cascade* const* cascades,
                             int cascade_count, void* ripple_front_tex, int frame) = 0;
    virtual void encode_stamp(void* compute_encoder, const Config&, const StampList&, int frame) = 0;
    virtual void encode_readback(void* blit_encoder, const Config&, int frame) = 0;
    virtual void* world_grid_handle() const = 0;
    virtual void* surface_handle()    const = 0;
    virtual float height_at(float x, float z, const Config&, int frame) const = 0;
};
}
