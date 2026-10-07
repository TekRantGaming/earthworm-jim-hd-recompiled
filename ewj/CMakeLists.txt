# earthworm_jim_hd - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(earthworm_jim_hd VERSION 0.9.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# setjmp/longjmp are only called through veneers, so every codegen run must be
# followed by tools/patch_setjmp.ps1 (NOTES.md). setup.ps1 runs it; this makes
# the codegen step inside the build run it too, so a regenerate during a build
# can never drop the fix. The patch is idempotent.
if(WIN32)
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/codegen.build.stamp"
        APPEND
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/../tools/patch_setjmp.ps1"
                -Generated "${CMAKE_CURRENT_SOURCE_DIR}/generated/default")
endif()

# TRG Launcher (third_party/trg-launcher, a submodule). It draws inside the
# runtime's own Dear ImGui frame, so it must use the ImGui compiled into
# rexruntime (one ImGui context), never a downloaded copy.
add_library(ewj_imgui INTERFACE)
target_link_libraries(ewj_imgui INTERFACE rex::runtime)
set(TRG_LAUNCHER_IMGUI_TARGET ewj_imgui CACHE STRING "" FORCE)
set(TRG_LAUNCHER_STANDALONE OFF CACHE BOOL "" FORCE)
set(TRG_LAUNCHER_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(TRG_LAUNCHER_FETCH_DEPS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/trg-launcher trg-launcher)

# Sources
set(EARTHWORMJIMHD_SOURCES
    src/main.cpp
    src/abi_helpers.cpp
    src/profile_fix.cpp
    src/live_fix.cpp
    src/updater.cpp
    src/fpe_guard.cpp
    src/frame_stats.cpp
    src/sampler.cpp
    src/settings.cpp
    src/input_remap.cpp
    src/launcher.cpp
    src/platform.cpp
    src/stfs.cpp
    src/art.cpp
    src/overlay.cpp
    src/toast.cpp
)

# Developer-only tracing hooks (never in release builds): -DEWJ_DEV_TOOLS=ON.
option(EWJ_DEV_TOOLS "Build developer tracing hooks" OFF)
if(EWJ_DEV_TOOLS)
    list(APPEND EARTHWORMJIMHD_SOURCES src/dev_trace.cpp)
endif()

if(WIN32)
    add_executable(earthworm_jim_hd WIN32 ${EARTHWORMJIMHD_SOURCES})
else()
    add_executable(earthworm_jim_hd ${EARTHWORMJIMHD_SOURCES})
endif()

rexglue_setup_target(earthworm_jim_hd GPU_PLUGINS xenos)
target_link_libraries(earthworm_jim_hd PRIVATE trg::launcher)
# The launcher's updater compares this with the latest GitHub release tag.
target_compile_definitions(earthworm_jim_hd PRIVATE EWJ_VERSION="${PROJECT_VERSION}")
if(EWJ_DEV_TOOLS)
    target_compile_definitions(earthworm_jim_hd PRIVATE EWJ_DEV_TOOLS)
endif()
