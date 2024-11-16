cmake_minimum_required(VERSION 3.16)

project(gEngineEditor VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output directory
set(BINARIES_FOLDER ${CMAKE_CURRENT_SOURCE_DIR}/../Binaries)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../Binaries/$<CONFIG>)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../Binaries/$<CONFIG>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../Binaries/$<CONFIG>)

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

set(HEADERS
    GnMainWindow.h

    Viewport/Viewport.h
    Viewport/ViewportRenderer.h
)

set(SOURCES
    main.cpp
    GnMainWindow.cpp

    Viewport/Viewport.cpp
    Viewport/ViewportRenderer.cpp
)

set(FORMS
    GnMainWindow.ui
)

qt5_add_resources(RESOURCES
    Resources/Resource.qrc
)

add_executable(${PROJECT_NAME} ${HEADERS} ${SOURCES} ${FORMS} ${RESOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    Viewport
)

# Core modules
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../Runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/../Runtime/Core
    ${CMAKE_CURRENT_SOURCE_DIR}/../Runtime/Classes
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Core
    Qt5::Widgets

    GnClassLibrary
    GnCore
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(WIN32)
    if(MSVC)
        add_custom_command(TARGET ${PROJECT_NAME} PRE_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Building configuration: $<CONFIG>"
        )
    endif()
elseif(APPLE)

elseif(LINUX)

endif()
