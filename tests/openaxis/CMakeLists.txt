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
find_package(Qt5 5.15 REQUIRED COMPONENTS Core Widgets OpenGL)
find_package(OpenGL REQUIRED)
target_link_libraries(picking_tests PRIVATE Qt5::Widgets Qt5::OpenGL OpenGL::GLU)
add_test(NAME openaxis_depth_picking COMMAND picking_tests)
set_tests_properties(openaxis_depth_picking PROPERTIES SKIP_RETURN_CODE 77)
add_executable(scheduler_tests scheduler_tests.cpp)
target_compile_features(scheduler_tests PRIVATE cxx_std_17)
target_include_directories(scheduler_tests PRIVATE
    "${OPENAXIS_SOURCE_DIR}/cpp/include" "${CMAKE_CURRENT_SOURCE_DIR}/../../src/meshlab")
target_link_libraries(scheduler_tests PRIVATE Qt5::Core)
add_test(NAME openaxis_qt_scheduler COMMAND scheduler_tests)
