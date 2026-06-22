#include <gtest/gtest.h>
#include "voxel/GridView.h"
#include "voxel/MaterialCa.h"   // ca_cell_index, MaterialCaDims
#include "voxel_grid.h"         // vg_cell_index, VoxelGridDesc
#include <vector>

TEST(GridView, AddressMatchesLinearIndex) {
    for (int E : {1, 2, 7, 16}) {
        for (int H : {1, 3, 9}) {
            std::vector<uint8_t> buf((size_t)E * H * E, 0);
            auto g = vox::grid_view(buf, E, H);
            const uint8_t* base = buf.data();
            VoxelGridDesc d{E, H, 0.5f, 0.25f, 10.0f};
            vox::MaterialCaDims cd{E, H};
            for (int z = 0; z < E; ++z)
                for (int y = 0; y < H; ++y)
                    for (int x = 0; x < E; ++x) {
                        auto off = &g[x, y, z] - base;
                        EXPECT_EQ(off, vg_cell_index(d, x, y, z)) << x << "," << y << "," << z;
                        EXPECT_EQ(off, vox::ca_cell_index(cd, x, y, z));
                    }
        }
    }
}

TEST(GridView, MutableViewWritesThroughToBuffer) {
    const int E = 4, H = 5;
    std::vector<uint8_t> buf((size_t)E * H * E, 0);
    auto g = vox::grid_view(buf, E, H);
    g[3, 4, 2] = 7;
    EXPECT_EQ(buf[vox::ca_cell_index({E, H}, 3, 4, 2)], 7);
}
