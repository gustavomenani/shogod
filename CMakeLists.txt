cmake_minimum_required(VERSION 3.25)
project(shogod LANGUAGES C CXX)

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
    src/features/math/hitbox_geom.cpp
    src/features/math/hitchance.cpp
    src/config/config.cpp
    src/config/keybinds.cpp
    src/sdk/snapshot.cpp
    src/features/backtrack.cpp
    src/features/rage/target.cpp
    src/features/aaa/antiaim.cpp
    src/features/audio/hitsounds.cpp
)

add_library(shogod_core STATIC ${SHOGOD_CORE_SOURCES})
target_include_directories(shogod_core PUBLIC src vendor)
target_compile_options(shogod_core PUBLIC ${SHOGOD_WARNINGS})
target_compile_definitions(shogod_core PUBLIC SHOGOD_EXPORTS NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(shogod_core PUBLIC shell32 ole32 windowscodecs winmm)

# ---- ui + render (DX11/ImGui surface, menu, pages) --------------------------
set(SHOGOD_UI_SOURCES
    src/engine/render/dx11.cpp
    src/features/esp/esp.cpp
    src/ui/style.cpp
    src/ui/widgets.cpp
    src/ui/menu.cpp
    src/ui/preview/model.cpp
    src/ui/preview/preview.cpp
    src/ui/pages/pages_common.cpp
    src/ui/pages/page_rage.cpp
    src/ui/pages/page_antiaim.cpp
    src/ui/pages/page_visuals.cpp
    src/ui/pages/page_misc.cpp
    src/ui/pages/page_configs.cpp
    src/ui/pages/page_settings.cpp
)

add_library(shogod_ui STATIC ${SHOGOD_UI_SOURCES})
target_link_libraries(shogod_ui PUBLIC shogod_core imgui d3d11 dxgi d3dcompiler)

add_library(shogod_minhook STATIC
    vendor/minhook/src/buffer.c
    vendor/minhook/src/hook.c
    vendor/minhook/src/trampoline.c
    vendor/minhook/src/hde/hde64.c
)
target_include_directories(shogod_minhook PUBLIC vendor/minhook/include)

# ---- shogod.dll -------------------------------------------------------------
add_library(shogod SHARED
    src/engine/dllmain.cpp
    src/sdk/signatures.cpp
    src/sdk/schema.cpp
    src/sdk/entities.cpp
    src/sdk/trace.cpp
    src/sdk/shot_events.cpp
    src/sdk/usercmd.cpp
    src/sdk/hitboxes.cpp
    src/features/lagcomp.cpp
    src/features/chams/chams.cpp
    src/features/world/world.cpp
    src/features/createmove.cpp
    src/engine/hooks.cpp
    src/engine/module.cpp
    src/engine/runtime.cpp
)
target_link_libraries(shogod PRIVATE shogod_ui shogod_minhook)
set_target_properties(shogod PROPERTIES
    OUTPUT_NAME "shogod"
    PREFIX ""
    SUFFIX ".dll"
    MSVC_RUNTIME_LIBRARY "MultiThreaded"
)
target_compile_options(shogod PRIVATE $<$<CONFIG:Release>:/O2 /Oi /Gy>)

# ---- menu host (visual gate, standalone) ------------------------------------
add_executable(shogod_host tools/menu_host/main.cpp)
target_link_libraries(shogod_host PRIVATE shogod_ui)
set_target_properties(shogod_host PROPERTIES OUTPUT_NAME "shogod_host")

add_executable(shogod_loader tools/loader/main.cpp)
set_target_properties(shogod_loader PROPERTIES OUTPUT_NAME "shogod_loader")
target_compile_options(shogod_loader PRIVATE /W4 /utf-8 /MP)

# ---- tests ------------------------------------------------------------------
option(SHOGOD_BUILD_TESTS "Build the unit-test harness" ON)
if(SHOGOD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
