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
    voxel_grid_test.cpp
    ${CMAKE_SOURCE_DIR}/src/voxel/VoxelGrid.cpp
)

include(GoogleTest)
gtest_discover_tests(vox-ocean-tests)
