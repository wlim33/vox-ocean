#pragma once
#include <array>
#include <glm/glm.hpp>
namespace vox {
struct WaveConfig;

// CPU analytical approximation of the GPU FFT ocean surface, for entity physics.
// Deterministic for a given WaveConfig; evaluated in world space, no GPU/readback.
// Mean sea level is y = 0 (matches the GPU surface_tex_ baseline). This trades
// fine-ripple fidelity for a strictly one-way CPU->GPU data flow.
class WaterModel {
public:
    // Rebuild the directional wave bank from wind/spectrum params. Cheap (a few
    // trig setups) -- safe to call every frame or only when WaveConfig changes.
    void configure(const WaveConfig& wave);
    // Surface y at world (x,z) and time t (seconds).
    float height_at(float x, float z, float t) const;
private:
    static constexpr int kWaves = 4;
    struct Wave {
        glm::vec2 dir{1.0f, 0.0f};  // unit propagation direction
        float amp   = 0.0f;         // meters
        float k     = 0.0f;         // wavenumber, 2*pi/wavelength
        float omega = 0.0f;         // angular frequency, sqrt(g*k)
        float phase = 0.0f;         // static phase offset
    };
    std::array<Wave, kWaves> waves_{};
};
}
