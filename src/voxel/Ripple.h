#pragma once
#include <vector>
namespace vox {
// Interactive ripple layer: damped 2D wave equation on the column grid.
// CPU mirror of shaders/ripple.metal — keep the update in lockstep.
// The sim advances one FIXED step (dt = 1/60 s) per rendered frame:
// stability + bench determinism over wall-clock accuracy.

// k = (c*dt/dx)^2, clamped to 0.45 (2D CFL stability bound is 0.5) so no
// knob combination can blow the sim up.
float ripple_k(float wave_speed_mps, float dt_s, float dx_m);

// next = damping * (2*cur - prev + k * laplacian4(cur)), clamped-edge
// neighbors, with an extra absorbing ramp over the outer 8 cells so waves
// die at the diorama wall instead of reflecting.
void ripple_step(const std::vector<float>& prev, const std::vector<float>& cur,
                 std::vector<float>& next, int extent, float k, float damping);
}
