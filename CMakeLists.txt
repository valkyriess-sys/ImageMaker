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
)

target_include_directories(imagemaker PRIVATE
    ${Vulkan_INCLUDE_DIR}
    src
)

target_link_libraries(imagemaker PRIVATE
    Vulkan::Vulkan
    glm::glm-header-only
    glfw
)
