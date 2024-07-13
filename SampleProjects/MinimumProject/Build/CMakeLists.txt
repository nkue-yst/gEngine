cmake_minimum_required(VERSION 3.20.0)

project(MinimumProject CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PLAY_IN_EDITOR OFF)

set(PROJECT_SOURCE_DIR ${CMAKE_SOURCE_DIR}/../Source)

### ヘッダーサーチパス ###
set(HEADER_SEARCH_PATH
    "${CMAKE_SOURCE_DIR}/../../../Engine/Source/Runtime/Base"
    "${CMAKE_SOURCE_DIR}/../../../Engine/Source/Runtime/Core/Public"
    "${CMAKE_SOURCE_DIR}/../../../Engine/Source/Runtime/Engine/Public"
)
include_directories(${HEADER_SEARCH_PATH})

if(NOT PLAY_IN_EDITOR)
    ### プロジェクト実装ファイルを静的ライブラリとしてビルド ###
    add_library(${PROJECT_NAME}_module STATIC
        ${PROJECT_SOURCE_DIR}/${PROJECT_NAME}.cpp

        ${CMAKE_BINARY_DIR}/x64/
    )
    target_include_directories(${PROJECT_NAME}_module PRIVATE ${PROJECT_SOURCE_DIR} ${HEADER_SEARCH_PATH})
    target_link_libraries(${PROJECT_NAME}_module PRIVATE ${CMAKE_SOURCE_DIR}/../../../build/Engine/Source/Runtime/gengine_runtime)

    ### プロジェクトのmain処理を実行ファイルとしてビルド ###
    add_executable(${PROJECT_NAME}
        ${PROJECT_SOURCE_DIR}/${PROJECT_NAME}.gnmain.cpp
    )
    target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR} ${HEADER_SEARCH_PATH})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_module ${CMAKE_SOURCE_DIR}/../../../build/Engine/Source/Runtime/gengine_runtime)
endif()
