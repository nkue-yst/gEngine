cmake_minimum_required(VERSION 3.16)

project(gEngineRuntime)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build Configuration
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../Binaries/$<CONFIG>)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../Binaries/$<CONFIG>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../Binaries/$<CONFIG>)

# Common include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
set(COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/gn_dll.h)

# "Classes"
## Classes Modules
set(CLASSES_MODULE
    SGameInstance
)

## Build ClassLibrary
foreach(MODULE IN LISTS CLASSES_MODULE)
    file(GLOB_RECURSE MODULE_HEADERS Classes/${MODULE}/*.h)
    file(GLOB_RECURSE MODULE_SOURCES Classes/${MODULE}/*.cpp)

    list(APPEND ClassLibrary_SOURCES ${MODULE_HEADERS} ${MODULE_SOURCES} ${COMMON_SOURCES})
endforeach()

add_library(GnClassLibrary SHARED ${ClassLibrary_SOURCES})


# "Core"
## Core Modules
set(CORE_MODULE
    Main

    CoreManager
)

## Build Core Module
foreach(MODULE IN LISTS CORE_MODULE)
    file(GLOB_RECURSE MODULE_HEADERS Core/${MODULE}/*.h)
    file(GLOB_RECURSE MODULE_SOURCES Core/${MODULE}/*.cpp)

    list(APPEND GnCore_SOURCES ${MODULE_HEADERS} ${MODULE_SOURCES} ${COMMON_SOURCES})
endforeach()

add_library(GnCore SHARED ${GnCore_SOURCES})

add_definitions(-DGENGINE_RUNTIME_DLL)
