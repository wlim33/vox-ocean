# vox-ocean

![macOS](https://img.shields.io/badge/macOS-13%2B-lightgrey)
![iOS](https://img.shields.io/badge/iOS-16%2B-silver)
![Metal](https://img.shields.io/badge/Metal-3-blue)
![C++](https://img.shields.io/badge/C%2B%2B-20-00599C)
![License](https://img.shields.io/badge/license-MIT-green)

Real-time voxelized FFT ocean for macOS and iOS. Metal + SwiftUI,
C++20 / Objective-C++ engine with direct Swift↔C++ interop.

- Phillips spectrum with directional swell, Stockham radix-2 inverse FFT in compute
- CPU-authoritative voxel world: the dense terrain+entity material grid (sand,
  rock, boat, kelp, fish) is owned on the CPU and mirrored to the GPU each frame
  as a compact edit delta; procedural seeded ocean floor, floor-quantized
- Ray-marched rendering: full-screen DDA through the GPU grid, with water
  reconstructed analytically from the per-column surface height — refraction +
  Beer–Lambert transmission through the water volume, composited over the
  sky; scalable march resolution (`march.render_scale`)
- Interactive ripple layer: damped wave-equation sim summed with the FFT,
  splash injection (`ripple.rain_rate`), absorbing diorama borders
- Autonomous voxel boat: hull composited into the CPU world, buoyancy from a
  CPU analytical wave model, deterministic wandering course, stern wake in the ripple layer
- Living ecosystem: a seeded kelp bed rooted across the floor that sways with
  the live water field (and the boat's wake), and fish in deterministic
  wandering schools — all composited into the CPU world and shipped to the GPU
  in a single per-frame edit list
- Finite diorama with volumetric Beer–Lambert attenuation along the in-water path, Jacobian foam on crest voxels
- Preetham sky baked to a cubemap; ACES tonemap

## Architecture

The engine keeps a hard wall between **world state (CPU)** and **render state
(GPU)**, joined by a single one-way interface — the CPU is authoritative and
nothing flows back from the GPU.

```
┌──────────────── CPU (authoritative, Metal-free, unit-tested) ───────────────┐
│  World         dense discrete grid: terrain + entities, mutated each frame   │
│  WaterModel    analytical wave height for buoyancy/physics (no GPU readback) │
│  Ecosystem     boat / fish / kelp — query WaterModel, composite into World   │
│      └─ build_frame() ─► RenderFrame { EditList edits; WaterState water; }    │
└────────────────────────────────────┬────────────────────────────────────────┘
                                      │  consume_frame(cb)        one way ▼
┌──────────────────────────────────────▼──────────────────── GPU (Metal) ──────┐
│  discrete_grid_   persistent terrain+entity texture; apply_edits applies the  │
│                   per-frame EditList (full re-upload of World on resync)      │
│  FFT + ripple     driven by WaterState → surface_tex_ (per-column water top)  │
│  raymarch         DDA over discrete_grid_; water DERIVED in-shader from       │
│                   surface_tex_ (an air cell below the surface top is water)   │
└────────────────────────────────────────────────────────────────────────────────┘
```

- **`build_frame()`** (CPU) advances the sim and entities, writes them into
  `World`, and diffs `World`'s discrete grid against the previous frame to emit
  an `EditList` (only the cells that changed).
- **`consume_frame(cb)`** (GPU) encodes everything: `apply_edits` updates the
  persistent `discrete_grid_` from the edit list, the FFT/ripple passes write
  `surface_tex_`, and the raymarch renders the grid with water composited as a
  surface layer rather than stored as voxels.
- The camera is a per-view argument, not part of `RenderFrame` (the headless
  `--snapshot` renders one built frame from six orthographic cameras).

The split makes the world logic testable without Metal and lets the two halves
evolve independently behind the `RenderFrame` boundary.

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
