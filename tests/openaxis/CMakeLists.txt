cmake_minimum_required(VERSION 3.24)
project(MeshLabOpenAxisTests LANGUAGES CXX)
enable_testing()
if(NOT OPENAXIS_SOURCE_DIR)
    message(FATAL_ERROR "Set OPENAXIS_SOURCE_DIR to the SDK checkout used by MeshLab")
endif()
add_executable(camera_tests camera_tests.cpp)
target_compile_features(camera_tests PRIVATE cxx_std_17)
target_include_directories(camera_tests PRIVATE
    "${OPENAXIS_SOURCE_DIR}/cpp/include" "${CMAKE_CURRENT_SOURCE_DIR}/../../src/meshlab")
add_test(NAME openaxis_camera_mapping COMMAND camera_tests)
add_executable(picking_tests picking_tests.cpp)
target_compile_features(picking_tests PRIVATE cxx_std_17)
target_include_directories(picking_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/vcglib"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/vcglib/eigenlib"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/meshlab")
add_test(NAME openaxis_upstream_picking COMMAND picking_tests)
