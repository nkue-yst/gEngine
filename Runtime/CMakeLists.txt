cmake_minimum_required(VERSION 3.16)

project(gEngineRuntime)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Build Configuration
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../Binaries/$<CONFIG>)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../Binaries/$<CONFIG>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../Binaries/$<CONFIG>)

# Filter Settings
SET_PROPERTY(GLOBAL PROPERTY USE_FOLDERS ON) 

# Qt
if(NOT DEFINED ENV{QT_DIR})
    if(WIN32)
        message(FATAL_ERROR "環境変数\"QT_DIR\"にQtのインストールパス(/path/to/msvc2019_64/)を設定してください。")
    elseif(APPLE)

    elseif(LINUX)

    endif()
else()
    set(CMAKE_PREFIX_PATH $ENV{QT_DIR} $ENV{Qt5_DIR} $ENV{QT_ROOT_DIR})
endif()

find_package(Qt5 REQUIRED COMPONENTS Core Widgets)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DGENGINE_DEBUG")

# Common include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
set(COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/gn_dll.h)

# "Classes"
## Classes Modules
set(CLASSES_MODULE
    GameInstance
)

## Build ClassLibrary
foreach(MODULE IN LISTS CLASSES_MODULE)
    file(GLOB_RECURSE MODULE_HEADERS Classes/${MODULE}/*.h)
    file(GLOB_RECURSE MODULE_SOURCES Classes/${MODULE}/*.cpp)

    list(APPEND ClassLibrary_SOURCES ${MODULE_HEADERS} ${MODULE_SOURCES} ${COMMON_SOURCES})

    SOURCE_GROUP(${MODULE} FILES ${MODULE_HEADERS} ${MODULE_SOURCES})
endforeach()

add_library(GnClassLibrary SHARED ${ClassLibrary_SOURCES})


# "Core"
## Core Modules
set(CORE_MODULE
    Application
    CoreManager
    Logging
    Renderer
    Window
)

## Build Core Module
foreach(MODULE IN LISTS CORE_MODULE)
    file(GLOB_RECURSE MODULE_HEADERS Core/${MODULE}/*.h)
    file(GLOB_RECURSE MODULE_SOURCES Core/${MODULE}/*.cpp)

    list(APPEND GnCore_SOURCES ${MODULE_HEADERS} ${MODULE_SOURCES} ${COMMON_SOURCES})

    SOURCE_GROUP(${MODULE} FILES ${MODULE_HEADERS} ${MODULE_SOURCES})
endforeach()

add_library(GnCore SHARED ${GnCore_SOURCES})

foreach(MODULE IN LISTS CLASSES_MODULE)
    target_include_directories(GnCore PRIVATE Classes/${MODULE})
endforeach()

target_link_libraries(GnCore PRIVATE
    Qt5::Core
    Qt5::Widgets
)

add_definitions(-DGENGINE_RUNTIME_DLL)
