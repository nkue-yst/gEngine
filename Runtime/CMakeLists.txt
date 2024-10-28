cmake_minimum_required(VERSION 3.16)

project(gEngineRuntime)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# "Classes"
## Build Configuration
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../Binaries/$<CONFIG>/Runtime/Classes)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../Binaries/$<CONFIG>/Runtime/Classes)


## Classes Modules
set(CLASSES_MODULE
    SGameInstance
)

## Build static libraries
foreach(MODULE IN LISTS CLASSES_MODULE)
    add_subdirectory(Classes/${MODULE})
endforeach()


# Runtime library
## Build Configuration
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../Binaries/$<CONFIG>)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../Binaries/$<CONFIG>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../Binaries/$<CONFIG>)

add_library(${PROJECT_NAME} STATIC Runtime.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE ${CLASSES_MODULE})
