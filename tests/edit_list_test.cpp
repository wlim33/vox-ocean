#include "world/EditList.h"
#include <gtest/gtest.h>
#include <vector>

TEST(EditList, DiffEmptyWhenIdentical) {
    std::vector<uint8_t> a = {0, 1, 2, 3, 4};
    std::vector<uint8_t> b = a;
    vox::EditList e;
    vox::diff_cells(a, b, e);
    EXPECT_EQ(e.count(), 0);
    EXPECT_FALSE(e.resync);
}

TEST(EditList, DiffEmitsChangedCells) {
    std::vector<uint8_t> prev = {0, 1, 2, 3, 4};
    std::vector<uint8_t> cur  = {0, 9, 2, 8, 4};   // cells 1 and 3 changed
    vox::EditList e;
    vox::diff_cells(prev, cur, e);
    ASSERT_EQ(e.count(), 2);
    EXPECT_EQ(e.idx[0], 1u); EXPECT_EQ(e.mat[0], 9);
    EXPECT_EQ(e.idx[1], 3u); EXPECT_EQ(e.mat[1], 8);
}

TEST(EditList, ApplyReconstructsCurFromPrev) {
    std::vector<uint8_t> prev = {0, 1, 2, 3, 4};
    std::vector<uint8_t> cur  = {5, 1, 7, 3, 0};
    vox::EditList e;
    vox::diff_cells(prev, cur, e);
    std::vector<uint8_t> grid = prev;   // start from prev, apply the delta
    vox::apply_edits(grid, e);
    EXPECT_EQ(grid, cur);
}

TEST(EditList, ApplyTouchesOnlyEditedCells) {
    std::vector<uint8_t> grid = {0, 0, 0, 0};
    vox::EditList e;
    e.push(2, 42);
    vox::apply_edits(grid, e);
    EXPECT_EQ(grid, (std::vector<uint8_t>{0, 0, 42, 0}));
}

TEST(EditList, ClearResetsResync) {
    vox::EditList e;
    e.push(1, 1);
    e.resync = true;
    e.clear();
    EXPECT_EQ(e.count(), 0);
    EXPECT_FALSE(e.resync);
}
