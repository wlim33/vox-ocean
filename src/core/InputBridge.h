#pragma once
#include "core/InputEvent.h"
#include <mutex>
#include <utility>
#include <vector>

namespace vox {
// Thread-safe queue: Swift shell pushes (via Engine), App drains each frame.
class InputBridge {
public:
    void push(const InputEvent& e) {
        std::lock_guard<std::mutex> lk(mu_);
        events_.push_back(e);
    }
    std::vector<InputEvent> drain() {
        std::lock_guard<std::mutex> lk(mu_);
        return std::exchange(events_, {});
    }
private:
    std::mutex mu_;
    std::vector<InputEvent> events_;
};
}
