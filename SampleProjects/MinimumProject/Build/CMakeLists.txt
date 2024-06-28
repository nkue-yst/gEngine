cmake_minimum_required(VERSION 3.20.0)

project(MinimumProject CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../Binaries)

set(PROJECT_SOURCE_DIR ${CMAKE_SOURCE_DIR}/../Source)
add_executable(MinimumProject
    ${PROJECT_SOURCE_DIR}/${PROJECT_NAME}.cpp
    ${PROJECT_SOURCE_DIR}/${PROJECT_NAME}.gnmain.cpp
)
