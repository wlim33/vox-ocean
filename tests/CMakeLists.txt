add_executable(vox-ocean-tests
    config_test.cpp
)
target_include_directories(vox-ocean-tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vox-ocean-tests PRIVATE
    gtest_main tomlplusplus::tomlplusplus glm::glm)

# Pull in source files needed by tests (no Metal/Obj-C++)
target_sources(vox-ocean-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/Config.cpp
)

target_sources(vox-ocean-tests PRIVATE
    camera_test.cpp
    ${CMAKE_SOURCE_DIR}/src/core/OrbitCamera.cpp
)

target_sources(vox-ocean-tests PRIVATE
    voxel_world_test.cpp
    ${CMAKE_SOURCE_DIR}/src/voxel/VoxelWorld.cpp
)

target_sources(vox-ocean-tests PRIVATE
    fft_reference_test.cpp
    support/cpu_fft_reference.cpp
)
target_include_directories(vox-ocean-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# GPU/CPU parity: executes the butterfly schedule shared with shaders/fft.metal.
target_sources(vox-ocean-tests PRIVATE fft_parity_test.cpp)
target_include_directories(vox-ocean-tests PRIVATE ${CMAKE_SOURCE_DIR}/shaders)

target_sources(vox-ocean-tests PRIVATE
    spectrum_test.cpp
    ${CMAKE_SOURCE_DIR}/src/ocean/Spectrum.cpp
)

target_sources(vox-ocean-tests PRIVATE
    bench_test.cpp
    ${CMAKE_SOURCE_DIR}/src/bench/BenchmarkHarness.cpp
    ${CMAKE_SOURCE_DIR}/src/bench/BenchPaths.cpp
)

target_sources(vox-ocean-tests PRIVATE
    floor_gen_test.cpp
    ${CMAKE_SOURCE_DIR}/src/voxel/FloorGen.cpp
)

target_sources(vox-ocean-tests PRIVATE
    dda_test.cpp
    ${CMAKE_SOURCE_DIR}/src/voxel/Dda.cpp
)

target_sources(vox-ocean-tests PRIVATE
    ripple_test.cpp
    ${CMAKE_SOURCE_DIR}/src/voxel/Ripple.cpp
)

target_sources(vox-ocean-tests PRIVATE
    boat_test.cpp
    ${CMAKE_SOURCE_DIR}/src/entity/Boat.cpp
)

include(GoogleTest)
gtest_discover_tests(vox-ocean-tests)
