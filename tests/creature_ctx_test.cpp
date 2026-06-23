// tests/creature_ctx_test.cpp
#include "entity/Creature.h"
#include "entity/CreatureRegistry.h"
#include "world/World.h"
#include "voxel/VoxelWorld.h"
#include "core/Config.h"
#include <gtest/gtest.h>

using namespace vox;

static Config ctx_cfg() {
    Config c;
    c.voxel.grid_extent = 32; c.voxel.height_cells = 64;
    c.voxel.voxel_size_m = 0.5f; c.voxel.height_step_m = 0.25f; c.voxel.base_depth_m = 10.0f;
    c.kelp.enabled = false; c.fish.enabled = false; c.entity.boat_enabled = false;
    return c;
}

// Build a ctx over a configured world with an optional seeded cell.
TEST(CreatureCtx, SampleReturnsSeededMaterialAndAirOutOfBounds) {
    Config c = ctx_cfg();
    World world; world.configure(c);
    const VoxelWorld& g = world.grid();
    int ix = 10, iy = 30, iz = 12;
    world.apply_user_edit((uint32_t)g.cell_index(ix, iy, iz), (uint8_t)VoxMat::Fire);

    CreatureRegistry reg;
    CreatureCtx ctx{ c, 1.0f/60.0f, 0.0f, world, g,
                     [](float,float){ return 0.0f; }, reg };

    float x = g.column_center_x(ix);
    float z = g.column_center_z(iz);
    float y = g.cell_bottom_y(iy) + 0.5f * c.voxel.height_step_m;
    EXPECT_EQ(ctx.sample(x, y, z), VoxMat::Fire);
    EXPECT_EQ(ctx.sample(1e6f, 1e6f, 1e6f), VoxMat::Air);    // out of grid
}

TEST(CreatureCtx, FindNearestLocatesHazardWithinRadius) {
    Config c = ctx_cfg();
    World world; world.configure(c);
    const VoxelWorld& g = world.grid();
    int ix = 16, iy = 40, iz = 16;
    world.apply_user_edit((uint32_t)g.cell_index(ix, iy, iz), (uint8_t)VoxMat::Fire);

    CreatureRegistry reg;
    CreatureCtx ctx{ c, 1.0f/60.0f, 0.0f, world, g,
                     [](float,float){ return 0.0f; }, reg };

    glm::vec3 from{ g.column_center_x(ix), g.cell_bottom_y(iy), g.column_center_z(iz) };
    auto hit = ctx.find_nearest(from, 2.0f,
                                [](VoxMat m){ return m == VoxMat::Fire; });
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->x, g.column_center_x(ix), 0.5f);
    EXPECT_NEAR(hit->z, g.column_center_z(iz), 0.5f);

    auto miss = ctx.find_nearest({-1000.0f, 0.0f, -1000.0f}, 2.0f,
                                 [](VoxMat m){ return m == VoxMat::Fire; });
    EXPECT_FALSE(miss.has_value());
}

TEST(CreatureCtx, ForEachNeighborDelegatesToRegistry) {
    Config c = ctx_cfg();
    World world; world.configure(c);
    CreatureRegistry reg;
    reg.add(CreaturePresence{ {1.0f, 0.0f, 0.0f}, 0.0f, Species_Minnow, 0 });
    CreatureCtx ctx{ c, 1.0f/60.0f, 0.0f, world, world.grid(),
                     [](float,float){ return 0.0f; }, reg };
    int hits = 0;
    ctx.for_each_neighbor({0.0f,0.0f,0.0f}, 3.0f,
                          [&](const CreaturePresence&){ ++hits; });
    EXPECT_EQ(hits, 1);
}
