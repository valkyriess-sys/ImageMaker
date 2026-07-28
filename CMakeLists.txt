cmake_minimum_required(VERSION 3.21)
project(ImageMaker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# vcpkg toolchain
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")

# Vulkan
find_package(Vulkan REQUIRED)

# glm
find_package(glm CONFIG REQUIRED)

# GLFW
find_package(glfw3 CONFIG REQUIRED)

add_executable(imagemaker
    src/main.cpp
    vendor/imgui/imgui.cpp
    vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_tables.cpp
    vendor/imgui/imgui_widgets.cpp
    vendor/imgui/backends/imgui_impl_glfw.cpp
    vendor/imgui/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imagemaker PRIVATE
    ${Vulkan_INCLUDE_DIR}
    src
    /home/valkyrie/.local/include
    vendor/imgui
    vendor/imgui/backends
)

target_link_libraries(imagemaker PRIVATE
    Vulkan::Vulkan
    glm::glm-header-only
    glfw
)

# M4: standalone export test (no Vulkan/GLFW — headless-friendly)
add_executable(test_m4_export
    tests/test_m4_export.cpp
)
target_include_directories(test_m4_export PRIVATE
    ${Vulkan_INCLUDE_DIR}
    src
)
target_link_libraries(test_m4_export PRIVATE
    glm::glm-header-only
)
