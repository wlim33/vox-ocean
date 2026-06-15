#pragma once
#include "voxel/VoxelField.h"
#include "gpu/Buffer.h"
#include "gpu/Texture.h"
namespace vox {
struct PipelineCache;

class DenseVoxelField : public VoxelField {
public:
    static constexpr int RING = 3;
    void init(const MetalContext&, PipelineCache&);
    VoxelGridDesc desc(const Config&) const override;
    void rebuild_if_dirty(const MetalContext&, const Config&) override;
    void upload_terrain_if_dirty(void* command_buffer) override;
    void ensure_capacity(const MetalContext&, const Config&) override;
    void encode_fill(void*, const Config&, Cascade* const*, int, void* ripple_front_tex, int) override;
    void encode_stamp(void*, const Config&, const StampList&, int) override;
    void encode_readback(void*, const Config&, int) override;
    void* world_grid_handle() const override { return world_grid_.handle; }
    void* surface_handle()    const override { return surface_tex_.handle; }
    float height_at(float x, float z, const Config&, int frame) const override;
    // Dev gate: full-rebuild into a scratch grid + diff vs the live grid; logs mismatches.
    void encode_verify(void* compute_encoder, const Config&, Cascade* const* cascades,
                       int cascade_count, void* ripple_front_tex, const StampList&, int frame);
private:
    Texture terrain_grid_{}, world_grid_{}, surface_tex_{};
    Buffer  terrain_staging_{};
    Buffer  fill_uniforms_[RING]{};
    Buffer  stamp_uniforms_[RING]{}, stamp_cells_[RING]{}, stamp_mats_[RING]{};
    Buffer  surface_readback_[RING]{};
    int     built_stamp_cap_ = 0;
    int     built_extent_ = 0, built_height_cells_ = 0, built_seed_ = 0;
    bool    terrain_dirty_ = false;
    void*   pso_fill_  = nullptr;
    void*   pso_stamp_ = nullptr;
    Texture world_grid_verify_{};        // scratch full-rebuild grid (verify_fill only)
    Buffer  diff_count_[RING]{};         // atomic mismatch counter readback ring
    void*   pso_diff_  = nullptr;
};
}
