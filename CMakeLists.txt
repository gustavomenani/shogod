cmake_minimum_required(VERSION 3.25)
project(shogod LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT MSVC)
    message(FATAL_ERROR "shogod targets MSVC x64 only")
endif()

set(SHOGOD_WARNINGS /W4 /permissive- /Zc:preprocessor /utf-8 /MP)

# ---- vendor -----------------------------------------------------------------
add_library(imgui STATIC
    vendor/imgui/imgui.cpp
    vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_tables.cpp
    vendor/imgui/imgui_widgets.cpp
    vendor/imgui/backends/imgui_impl_dx11.cpp
    vendor/imgui/backends/imgui_impl_win32.cpp
)
target_include_directories(imgui PUBLIC vendor/imgui vendor/imgui/backends)
target_compile_options(imgui PRIVATE ${SHOGOD_WARNINGS} /W3)
target_link_libraries(imgui PUBLIC d3d11 dxgi)

# ---- core (everything testable, no hooks, no game) --------------------------
set(SHOGOD_CORE_SOURCES
    src/brand.cpp
    src/engine/log.cpp
    src/engine/pattern.cpp
    src/features/math/angles.cpp
    src/features/math/hull.cpp
    src/features/math/hitchance.cpp
    src/config/config.cpp
)

add_library(shogod_core STATIC ${SHOGOD_CORE_SOURCES})
target_include_directories(shogod_core PUBLIC src vendor)
target_compile_options(shogod_core PUBLIC ${SHOGOD_WARNINGS})
target_compile_definitions(shogod_core PUBLIC SHOGOD_EXPORTS NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(shogod_core PUBLIC shell32 ole32)

# ---- shogod.dll -------------------------------------------------------------
add_library(shogod SHARED
    src/engine/dllmain.cpp
)
target_link_libraries(shogod PRIVATE shogod_core imgui)
set_target_properties(shogod PROPERTIES
    OUTPUT_NAME "shogod"
    PREFIX ""
    SUFFIX ".dll"
    MSVC_RUNTIME_LIBRARY "MultiThreaded"
)
# Product image: optimised, no debug heap, no /GS cookies on hot paths.
target_compile_options(shogod PRIVATE $<$<CONFIG:Release>:/O2 /Oi /Gy>)

# ---- tests ------------------------------------------------------------------
option(SHOGOD_BUILD_TESTS "Build the unit-test harness" ON)
if(SHOGOD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
