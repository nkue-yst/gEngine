cmake_minimum_required(VERSION 3.22)
project(gEngine VERSION 0.1.0 LANGUAGES C CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "gEngine component integration requires Windows x64 and MSVC")
endif()

option(GENGINE_BUILD_TESTS "Build component, VM and action integration tests" ON)
if(GENGINE_BUILD_TESTS)
    enable_testing()
endif()

# Rustの標準ライブラリとCRTを揃える。Debugの最適化・assert・PDBは維持する。
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
include(cmake/gEngineVm.cmake)
include(cmake/gEngineQt.cmake)
add_subdirectory(src/Editor EXCLUDE_FROM_ALL)

# 部品の単独ビルド設定を保ち、親からは必要なターゲットだけを構築する。
add_subdirectory(components/aEngine EXCLUDE_FROM_ALL)
add_subdirectory(components/rEngine EXCLUDE_FROM_ALL)
set(BINPACKLIB_BUILD_TESTS OFF CACHE BOOL "Build BinPackLib tests" FORCE)
set(BINPACKLIB_BUILD_TOOLS ON CACHE BOOL "Build sample pack tool" FORCE)
add_subdirectory(components/BinPackLib EXCLUDE_FROM_ALL)
include(cmake/gEngineComponents.cmake)

add_subdirectory(apps/Editor)
add_subdirectory(src/Runtime)
add_subdirectory(apps/GameRunner)
gengine_use_components(gengine_editor)

# Editorのビルド操作は、同じ構成で生成した既存compiler・packツール・Runnerを使う。
add_custom_target(gengine_build_tools
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:gengine_runner>" "$<TARGET_FILE_DIR:gengine_editor>/gengine_runner.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:bpack>" "$<TARGET_FILE_DIR:gengine_editor>/bpack.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:bpack>" "${CMAKE_BINARY_DIR}/$<CONFIG>/bpack.exe"
    DEPENDS gengine_runner bpack VERBATIM)
add_dependencies(gengine_editor gengine_build_tools)
include(cmake/gEngineDistribution.cmake)

if(GENGINE_BUILD_TESTS)
    add_subdirectory(tests)
    add_dependencies(gengine_qt_editor_test gengine_build_tools)
endif()
