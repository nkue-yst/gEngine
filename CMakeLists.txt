cmake_minimum_required(VERSION 3.16)

project(gEngine)

# Build Configuration
set(CMAKE_CONFIGURATION_TYPES "Debug;Release")

add_subdirectory(Runtime)
add_subdirectory(Editor)
