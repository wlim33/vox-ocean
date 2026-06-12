#include "bench/BenchmarkHarness.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>

TEST(Bench, InactiveWithoutBenchMode) {
    vox::Config cfg;                       // bench_mode defaults false
    vox::BenchmarkHarness h;
    h.start(cfg, 0x123);
    EXPECT_FALSE(h.active());
    EXPECT_FALSE(h.should_exit());
}
TEST(Bench, WritesHyperparameterHeaderAndExitsAfterMeasure) {
    vox::Config cfg;
    cfg.bench.bench_mode = true;
    cfg.bench.warmup_frames = 1;
    cfg.bench.measure_frames = 2;
    cfg.bench.output_path = "/tmp/vox-bench-test.csv";
    vox::BenchmarkHarness h;
    h.start(cfg, 0xABCDEF);
    EXPECT_TRUE(h.active());
    for (int i = 0; i < 3 && !h.should_exit(); ++i)
        h.record({h.current_frame(), 1.0, 2.0, 0.1});
    EXPECT_TRUE(h.should_exit());
    std::ifstream in("/tmp/vox-bench-test.csv");
    std::string first; std::getline(in, first);
    EXPECT_EQ(first.rfind("# config_hash=", 0), 0u);
    EXPECT_NE(first.find("grid_extent=192"), std::string::npos);
    EXPECT_NE(first.find("fft_size=256"), std::string::npos);
    std::remove("/tmp/vox-bench-test.csv");
}
