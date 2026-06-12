# vox-ocean

![macOS](https://img.shields.io/badge/macOS-13%2B-lightgrey)
![iOS](https://img.shields.io/badge/iOS-16%2B-silver)
![Metal](https://img.shields.io/badge/Metal-3-blue)
![C++](https://img.shields.io/badge/C%2B%2B-20-00599C)
![License](https://img.shields.io/badge/license-MIT-green)

Real-time voxelized FFT ocean for macOS and iOS. Metal + SwiftUI,
C++20 / Objective-C++ engine with direct Swift↔C++ interop.

- Phillips spectrum with directional swell, Stockham radix-2 inverse FFT in compute
- True voxel world: per-frame compute fills a 3D material grid (water, sand, rock)
  from the displacement maps, floor-quantized; procedural seeded ocean floor
- Ray-marched rendering: full-screen DDA through the grid, refraction +
  Beer–Lambert transmission through the water volume, composited over the
  sky; scalable march resolution (`march.render_scale`)
- Finite diorama with Beer–Lambert depth-tinted side walls, Jacobian foam on crest voxels
- Preetham sky baked to a cubemap; ACES tonemap

## Building

macOS 13+ / iOS 16+, Xcode 15+, [XcodeGen](https://github.com/yonaskolb/XcodeGen),
CMake 3.24+ for the test suite. Dependencies are git submodules.

    git clone --recursive <repo-url>
    xcodegen generate
    xcodebuild -project vox-ocean.xcodeproj -scheme vox-ocean-mac build

## Running

Drag to orbit, scroll (pinch on iOS) to zoom. Parameters are live sliders in
the ImGui panel. Settings load from `default-config.toml`; override with
`--config <file>` or repeatable `--set`:

    vox-ocean-mac --set voxel.grid_extent=256 --set wave.wind_speed_mps=20

## Benchmarking

    vox-ocean-mac --set bench.bench_mode=true

runs a deterministic camera orbit (60 warm-up + 600 measured frames) and
writes per-frame CPU/GPU timings to `bench-<timestamp>.csv` (working
directory on macOS, app Documents on iOS). The CSV header records the active
hyperparameters: the `voxel` grid dimensions and floor seed, the `march`
step budget and render scale, `wave.max_wavelength_m`, cascade count and
FFT size.

## Tests

The simulation and voxel core are plain C++ with no Metal dependency:

    cmake -B build && cmake --build build && ctest --test-dir build

## References

- Jerry Tessendorf, *Simulating Ocean Water*, SIGGRAPH course notes, 2001
- A. J. Preetham, Peter Shirley, Brian Smits, *A Practical Analytic Model for Daylight*, SIGGRAPH 1999

MIT licensed.
