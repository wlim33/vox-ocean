#include "voxel/FloorGen.h"
#include "voxel/VoxelWorld.h"
#include <gtest/gtest.h>
#include <set>

// 5-field FloorParams: extent, height_cells, seed, base_depth_m, height_step_m.
static vox::FloorParams params(uint32_t seed = 7u) {
    return vox::FloorParams{96, 64, seed, 10.0f, 0.25f};
}

TEST(FloorGen, DeterministicPerSeed) {
    auto p = params();
    auto a = vox::generate_floor(p);
    auto b = vox::generate_floor(p);
    ASSERT_EQ(a.size(), 96u * 96u);
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].height,   b[i].height);
        EXPECT_EQ(a[i].material, b[i].material);
    }
}

TEST(FloorGen, SeedsDiffer) {
    auto a = vox::generate_floor(params(7u));
    auto b = vox::generate_floor(params(8u));
    int diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff += a[i].height != b[i].height;
    EXPECT_GT(diff, (int)a.size() / 4);
}

TEST(FloorGen, HeightsVariedAndBounded) {
    auto p = params();
    auto f = vox::generate_floor(p);
    std::set<int> heights;
    for (auto& c : f) {
        EXPECT_GE((int)c.height, 1);
        EXPECT_LE((int)c.height, p.height_cells - 1);
        heights.insert(c.height);
    }
    EXPECT_GT(heights.size(), 4u);          // actually varied, not a flat slab
}

TEST(FloorGen, ShorelineRisesInTheCorner) {
    auto p = params();
    auto f = vox::generate_floor(p);
    int sea = vox::sea_level_cells(p.base_depth_m, p.height_step_m);   // 40
    auto at = [&](int ix, int iz) { return (int)f[(size_t)iz * p.extent + ix].height; };
    // Open-water corner keeps the low ocean floor (water headroom preserved).
    EXPECT_LE(at(0, 0), p.height_cells / 3);
    // Land corner rises above sea level.
    EXPECT_GT(at(p.extent - 1, p.extent - 1), sea);
}

TEST(FloorGen, WaterlineCrossesTheGrid) {
    auto p = params();
    auto f = vox::generate_floor(p);
    int sea = vox::sea_level_cells(p.base_depth_m, p.height_step_m);
    int below = 0, above = 0;
    for (auto& c : f) {
        if ((int)c.height < sea)      below++;
        else if ((int)c.height > sea) above++;
    }
    EXPECT_GT(below, (int)f.size() / 10);   // plenty of open water
    EXPECT_GT(above, (int)f.size() / 20);   // a real landmass above water
}

TEST(FloorGen, ContainsSandAndRock) {
    // Scan a few seeds: every floor is mostly sand; at least one seed in the
    // batch grows rock outcrops (rock is sparse by design).
    bool saw_rock = false;
    for (uint32_t seed = 0; seed < 10; ++seed) {
        auto f = vox::generate_floor(params(seed));
        int sand = 0, rock = 0;
        for (auto& c : f) {
            if (c.material == (uint8_t)vox::VoxMat::Sand) sand++;
            if (c.material == (uint8_t)vox::VoxMat::Rock) rock++;
        }
        EXPECT_EQ(sand + rock, (int)f.size());
        EXPECT_GT(sand, rock);              // sand dominates
        if (rock > 0) saw_rock = true;
    }
    EXPECT_TRUE(saw_rock);
}
