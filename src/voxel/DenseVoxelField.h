#pragma once
#include "voxel/VoxelField.h"
#include "gpu/Buffer.h"
#include "gpu/Texture.h"
#include "world/EditList.h"
namespace vox {
struct PipelineCache;

class DenseVoxelField : public VoxelField {
public:
    // RING > max in-flight frames (3): encode_destamp adds a reader of the stamp
    // ring one frame AFTER encode_stamp, so a slot's latest reader is frame f+1;
    // it must not be reused until f+1 completes. With <=3 in flight, reuse at
    // f+RING waits for f+RING-3, so RING must be >= 4.
    static constexpr int RING = 4;
    void init(const MetalContext&, PipelineCache&);
    VoxelGridDesc desc(const Config&) const override;
    void rebuild_if_dirty(const MetalContext&, const Config&) override;
    void upload_terrain_if_dirty(void* command_buffer,
                                 const std::vector<uint8_t>& terrain_cells) override;
    void ensure_capacity(const MetalContext&, const Config&) override;
    void encode_fill(void*, const Config&, Cascade* const*, int, void* ripple_front_tex, int) override;
    void encode_stamp(void*, const Config&, const StampList&, int) override;
    void encode_destamp(void* compute_encoder, const Config&, int frame);
    bool discrete_needs_resync(const EditList& edits) const;
    void encode_apply_edits(void* compute_encoder, const Config&, const EditList& edits, int frame);
    void encode_discrete_resync(void* blit_encoder, const Config&, const std::vector<uint8_t>& cells, int frame);
    void* world_grid_handle()    const override { return world_grid_.handle; }
    void* discrete_grid_handle() const override { return discrete_grid_.handle; }
    void* surface_handle()       const override { return surface_tex_.handle; }
    // Dev gate: full-rebuild into a scratch grid + diff vs the live grid; logs mismatches.
    void encode_verify(void* compute_encoder, const Config&, Cascade* const* cascades,
                       int cascade_count, void* ripple_front_tex, const StampList&, int frame);
    // Dev gate: upload World::cells() into a reference texture and diff vs discrete_grid_;
    // logs nonzero mismatch counts. Takes both encoders (blit for upload, compute for diff).
    void encode_verify_discrete(void* compute_encoder, void* blit_encoder, const Config&,
                                const std::vector<uint8_t>& cells, int frame);
private:
    Texture terrain_grid_{}, world_grid_{}, surface_tex_{};
    Buffer  terrain_staging_{};
    Buffer  fill_uniforms_[RING]{};
    Buffer  stamp_uniforms_[RING]{}, stamp_cells_[RING]{}, stamp_mats_[RING]{};
    int     built_stamp_cap_ = 0;
    int     built_extent_ = 0, built_height_cells_ = 0, built_seed_ = 0;
    float   built_base_depth_ = 0.0f, built_height_step_ = 0.0f;
    bool    terrain_dirty_ = false;
    void*   pso_fill_  = nullptr;
    void*   pso_stamp_ = nullptr;
    Buffer  prev_water_{};               // per-column previous water_top (sentinel -1 = seed)
    int     prev_stamp_count_ = 0;       // last frame's deduped stamp count (for destamp)
    void*   pso_incr_ = nullptr;
    void*   pso_destamp_ = nullptr;
    Texture world_grid_verify_{};        // scratch full-rebuild grid (verify_fill only)
    Buffer  diff_count_[RING]{};         // atomic mismatch counter readback ring
    void*   pso_diff_  = nullptr;
    Texture discrete_grid_{};                 // persistent terrain+entities mirror (no water)
    Buffer  discrete_staging_{};              // CPU->GPU staging for resync full uploads
    Buffer  edit_cells_[RING]{}, edit_mats_[RING]{}, apply_uniforms_[RING]{};
    int     built_edit_cap_ = 0;
    void*   pso_apply_edits_ = nullptr;
    Texture discrete_ref_{};                  // verify: fresh upload of World::cells() (verify_fill only)
    Buffer  discrete_ref_staging_{};          // verify: separate staging for discrete_ref_ (no alias with discrete_staging_)
    Buffer  discrete_diff_count_[RING]{};     // verify: mismatch counter ring for discrete_grid_ check
};
}
