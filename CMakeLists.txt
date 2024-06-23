cmake_minimum_required(VERSION 3.20)

project(gEngine)

### Runtime ###
set(RUNTIME_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Engine/Source/Runtime/")
add_subdirectory(${RUNTIME_ROOT_DIR})
