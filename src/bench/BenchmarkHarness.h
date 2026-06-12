#pragma once
#include "core/Config.h"
#include <atomic>
#include <fstream>
#include <string>
#include <chrono>
#include <cstdint>
namespace vox {

struct FrameTiming {
    int    frame_idx;
    double cpu_ms;
    double gpu_total_ms;
    double drawable_wait_ms;
};

class BenchmarkHarness {
public:
    void start(const Config& cfg, uint64_t config_hash);
    bool active() const { return active_; }
    void record(const FrameTiming& t);
    bool should_exit() const;
    int  current_frame() const { return frame_idx_; }
    int  warmup_frames() const { return warmup_; }
private:
    bool active_ = false;
    std::ofstream out_;
    int  warmup_ = 60;
    int  measure_ = 600;
    // Atomic: incremented by record() on the Metal completed-handler queue
    // while the main thread polls current_frame()/should_exit(). Handlers on
    // one queue are serialized, so out_ is only ever touched by one thread.
    std::atomic<int> frame_idx_{0};
    uint64_t hash_ = 0;
};
}
