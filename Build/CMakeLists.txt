cmake_minimum_required(VERSION 3.20)

project(gEngine)

### Runtime ###
add_subdirectory("${CMAKE_BINARY_DIR}/../Engine/Source/Runtime/" "${CMAKE_BINARY_DIR}/Engine/Runtime")
