#include "voxel/FloorGen.h"
#include "voxel/VoxelWorld.h"
#include <gtest/gtest.h>
#include <set>

TEST(FloorGen, DeterministicPerSeed) {
    vox::FloorParams p{96, 64, 7u};
    auto a = vox::generate_floor(p);
    auto b = vox::generate_floor(p);
    ASSERT_EQ(a.size(), 96u * 96u);
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].height,   b[i].height);
        EXPECT_EQ(a[i].material, b[i].material);
    }
}

TEST(FloorGen, SeedsDiffer) {
    auto a = vox::generate_floor({96, 64, 7u});
    auto b = vox::generate_floor({96, 64, 8u});
    int diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff += a[i].height != b[i].height;
    EXPECT_GT(diff, (int)a.size() / 4);
}

TEST(FloorGen, HeightsBoundedAndVaried) {
    vox::FloorParams p{96, 64, 7u};
    auto f = vox::generate_floor(p);
    std::set<int> heights;
    for (auto& c : f) {
        EXPECT_GE((int)c.height, 1);
        EXPECT_LE((int)c.height, 64 / 3);   // hills capped to the bottom third
        heights.insert(c.height);
    }
    EXPECT_GT(heights.size(), 4u);          // actually hilly, not a flat slab
}

TEST(FloorGen, ContainsSandAndRock) {
    // Scan a few seeds: every floor is mostly sand; at least one seed in the
    // batch grows rock outcrops (rock is sparse by design).
    bool saw_rock = false;
    for (uint32_t seed = 0; seed < 10; ++seed) {
        auto f = vox::generate_floor({96, 64, seed});
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
