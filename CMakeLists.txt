cmake_minimum_required(VERSION 3.16)

project(gEngine)

# Build Configuration
set(CMAKE_CONFIGURATION_TYPES "Debug;Release")

option(BUILD_WITH_APPLICATION "" ON)

add_subdirectory(Runtime)
add_subdirectory(Editor)

if(BUILD_WITH_APPLICATION)
    add_subdirectory(SampleApplication)
endif()
