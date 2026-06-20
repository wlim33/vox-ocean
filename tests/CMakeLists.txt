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

target_sources(vox-ocean-tests PRIVATE voxel_grid_test.cpp)

target_include_directories(vox-ocean-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(vox-ocean-tests PRIVATE ${CMAKE_SOURCE_DIR}/shaders)
# stb single-header PNG writer (used by PngWriter.cpp)
target_include_directories(vox-ocean-tests PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)

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
    boat_test.cpp
    ${CMAKE_SOURCE_DIR}/src/entity/Boat.cpp
)

target_sources(vox-ocean-tests PRIVATE
    stamp_budget_test.cpp
    ${CMAKE_SOURCE_DIR}/src/entity/StampBudget.cpp
)

target_sources(vox-ocean-tests PRIVATE
    wander_test.cpp
    ${CMAKE_SOURCE_DIR}/src/entity/Wander.cpp
)

target_sources(vox-ocean-tests PRIVATE
    kelp_test.cpp
    ${CMAKE_SOURCE_DIR}/src/entity/Kelp.cpp
)

target_sources(vox-ocean-tests PRIVATE
    fish_test.cpp
    ${CMAKE_SOURCE_DIR}/src/entity/Fish.cpp
)

target_sources(vox-ocean-tests PRIVATE
    ecosystem_test.cpp
    ${CMAKE_SOURCE_DIR}/src/entity/Ecosystem.cpp
    ${CMAKE_SOURCE_DIR}/src/world/World.cpp
)

target_sources(vox-ocean-tests PRIVATE
    png_writer_test.cpp
    ${CMAKE_SOURCE_DIR}/src/io/PngWriter.cpp
)

target_sources(vox-ocean-tests PRIVATE
    axis_view_test.cpp
    ${CMAKE_SOURCE_DIR}/src/core/AxisCamera.cpp
)

target_sources(vox-ocean-tests PRIVATE
    contact_sheet_test.cpp
    ${CMAKE_SOURCE_DIR}/src/io/ContactSheet.cpp
)

target_sources(vox-ocean-tests PRIVATE
    world_test.cpp
    ${CMAKE_SOURCE_DIR}/src/world/World.cpp
)

target_sources(vox-ocean-tests PRIVATE
    edit_list_test.cpp
    ${CMAKE_SOURCE_DIR}/src/world/EditList.cpp
)

target_sources(vox-ocean-tests PRIVATE
    material_ca_test.cpp
    ${CMAKE_SOURCE_DIR}/src/voxel/MaterialCa.cpp)

target_sources(vox-ocean-tests PRIVATE material_registry_test.cpp)

target_sources(vox-ocean-tests PRIVATE march_palette_test.cpp)

include(GoogleTest)
gtest_discover_tests(vox-ocean-tests)
