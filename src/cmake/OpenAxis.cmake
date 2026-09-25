option(MESHLAB_OPENAXIS "Enable Rotatrix OpenAxis navigation" ON)
set(OPENAXIS_SOURCE_DIR "" CACHE PATH "Optional OpenAxis checkout containing cpp/")
if(MESHLAB_OPENAXIS)
    if(CMAKE_VERSION VERSION_LESS 3.24)
        message(FATAL_ERROR "OpenAxis requires CMake 3.24 or newer")
    endif()
    set(OPENAXIS_BUILD_TESTS OFF CACHE BOOL "Build SDK tests separately")
    set(OPENAXIS_BUILD_DEMO OFF CACHE BOOL "Build SDK demo separately")
    if(OPENAXIS_SOURCE_DIR)
        add_subdirectory("${OPENAXIS_SOURCE_DIR}/cpp" "${PROJECT_BINARY_DIR}/openaxis")
    else()
        include(FetchContent)
        FetchContent_Declare(openaxis
            GIT_REPOSITORY https://github.com/rotatrix/openaxis.git
            # cpp/v1.0.0-rc.1, matching PrusaSlicer/backport-2.9.6
            GIT_TAG acc4da095cde6747556245b4b6c110c16b968b6b
            SOURCE_SUBDIR cpp)
        FetchContent_MakeAvailable(openaxis)
    endif()
    target_sources(meshlab PRIVATE openaxis_controller.cpp)
    target_link_libraries(meshlab PRIVATE OpenAxis::openaxis)
    target_compile_definitions(meshlab PRIVATE MESHLAB_OPENAXIS)
endif()
