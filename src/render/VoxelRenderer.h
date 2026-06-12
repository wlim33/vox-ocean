#pragma once
#include "gpu/Buffer.h"
#include <cstdint>
namespace vox {
struct MetalContext;
struct PipelineCache;
class  OrbitCamera;
struct Config;
class  Cascade;
class  SkyRenderer;

class VoxelRenderer {
public:
    void init(const MetalContext& ctx, PipelineCache& cache);
    // Recreates the instance buffer when voxel.grid_extent changes.
    void rebuild_if_dirty(const MetalContext& ctx, const Config& cfg);
    void encode_voxelize(void* compute_encoder, const Config& cfg,
                         Cascade* const* cascades, int cascade_count);
    void encode_draw(void* render_encoder, const OrbitCamera& cam, const Config& cfg,
                     const SkyRenderer& sky, int frame_index);
    static constexpr int RING = 3;
private:
    Buffer cube_vbo_{}, cube_ibo_{};
    Buffer instances_{};
    Buffer vox_uniforms_[RING]{}, draw_uniforms_[RING]{}, cam_buf_[RING]{};
    int    instance_extent_ = 0;
    int    index_count_ = 0;
    int    frame_index_ = 0;   // advanced by encode_voxelize; draw reuses the same slot
    void*  pso_voxelize_ = nullptr;
    void*  pso_draw_ = nullptr;
    void*  dss_ = nullptr;     // depth Less, write on
};
}
