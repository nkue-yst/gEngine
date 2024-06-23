cmake_minimum_required(VERSION 3.20)

enable_language(CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

project(gengine_runtime CXX)

### Header search path ###
set(HEADER_SEARCH_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/Base"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Public"
    "${CMAKE_CURRENT_SOURCE_DIR}/Engine/Public"
    "${CMAKE_CURRENT_SOURCE_DIR}/Engine/Internal"
)
include_directories(${HEADER_SEARCH_PATH})

### Add source files to the library ###
set(SOURCE_FILE_LIST
    "${CMAKE_CURRENT_SOURCE_DIR}/Base/SingletonBase.cpp"

    "${CMAKE_CURRENT_SOURCE_DIR}/Engine/Internal/Engine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Engine/Internal/gEngineMain.cpp"
)
add_library(${PROJECT_NAME} STATIC ${SOURCE_FILE_LIST})

foreach(SOURCE_FILE ${SOURCE_FILE_LIST})
    message(STATUS "Add source file: ${SOURCE_FILE}")
endforeach()
