cmake_minimum_required(VERSION 3.15)
project(space_sceneries CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to an optimized build: this is a CPU raytracer, and -O0 is ~10x slower.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# OpenMP parallelizes the per-pixel render loop across cores (optional).
find_package(OpenMP)

# --- core: math, geometry, raytracing, scene/JSON (no UI deps) ---
# nlohmann/json is vendored as a single header in third_party/.
add_library(core STATIC
    src/sphere.cpp
    src/disk.cpp
    src/render.cpp
    src/scene.cpp
    src/post.cpp
)
target_include_directories(core PUBLIC src third_party)
if(OpenMP_CXX_FOUND)
    target_link_libraries(core PUBLIC OpenMP::OpenMP_CXX)
endif()

# --- tests: headless sanity checks (Phases 1-3) ---
add_executable(space_sceneries_tests src/tests.cpp)
target_link_libraries(space_sceneries_tests PRIVATE core)

# --- editor GUI (Phase 4): GLFW + OpenGL + vendored Dear ImGui ---
find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR} ${IMGUI_DIR}/backends ${GLFW_INCLUDE_DIRS})

add_executable(space_sceneries src/app.cpp src/stb_image_write_impl.cpp)
target_link_libraries(space_sceneries PRIVATE core imgui OpenGL::GL ${GLFW_LIBRARIES})
target_include_directories(space_sceneries PRIVATE ${GLFW_INCLUDE_DIRS})
target_link_directories(space_sceneries PRIVATE ${GLFW_LIBRARY_DIRS})
