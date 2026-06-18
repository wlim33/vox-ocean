#pragma once
#include "voxel/VoxelField.h"
#include "gpu/Buffer.h"
#include "gpu/Texture.h"
#include "world/EditList.h"
namespace vox {
struct PipelineCache;

class DenseVoxelField : public VoxelField {
public:
    static constexpr int RING = 4;
    void init(const MetalContext&, PipelineCache&);
    VoxelGridDesc desc(const Config&) const override;
    void rebuild_if_dirty(const MetalContext&, const Config&) override;
    void ensure_capacity(const MetalContext&, const Config&) override;
    void encode_fill(void*, const Config&, Cascade* const*, int, void* ripple_front_tex, int) override;
    bool discrete_needs_resync(const EditList& edits) const;
    void encode_apply_edits(void* compute_encoder, const Config&, const EditList& edits, int frame);
    void encode_discrete_resync(void* blit_encoder, const Config&, const std::vector<uint8_t>& cells, int frame);
    void* discrete_grid_handle() const override { return discrete_grid_.handle; }
    void* surface_handle()       const override { return surface_tex_.handle; }
private:
    Texture surface_tex_{};
    Buffer  fill_uniforms_[RING]{};
    int     built_extent_ = 0, built_height_cells_ = 0, built_seed_ = 0;
    float   built_base_depth_ = 0.0f, built_height_step_ = 0.0f;
    void*   pso_fill_  = nullptr;
    Texture discrete_grid_{};                 // persistent terrain+entities mirror (no water)
    Buffer  discrete_staging_{};              // CPU->GPU staging for resync full uploads
    Buffer  edit_cells_[RING]{}, edit_mats_[RING]{}, apply_uniforms_[RING]{};
    int     built_edit_cap_ = 0;
    void*   pso_apply_edits_ = nullptr;
};
}
