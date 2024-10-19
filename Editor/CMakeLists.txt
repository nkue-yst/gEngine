cmake_minimum_required(VERSION 3.16)

project(gEngineEditor VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BINARIES_FOLDER ${CMAKE_CURRENT_SOURCE_DIR}/../Binaries)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BINARIES_FOLDER})

if(NOT DEFINED ENV{QT_DIR})
    message(FATAL_ERROR "環境変数\"QT_DIR\"にQtのインストールパス(/path/to/msvc2022_64/)を設定してください。")
else()
    set(CMAKE_PREFIX_PATH $ENV{QT_DIR})
endif()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(HEADERS
    GnMainWindow.h
)

set(SOURCES
    main.cpp
    GnMainWindow.cpp
)

set(FORMS
    GnMainWindow.ui
)

add_executable(${PROJECT_NAME} ${HEADERS} ${SOURCES} ${FORMS})

target_link_libraries(${PROJECT_NAME} PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.Editor)
endif()

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

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(${PROJECT_NAME})
endif()

if(WIN32)
    # windeploy
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${WINDEPLOYQT_EXECUTABLE} ${BINARIES_FOLDER}/${PROJECT_NAME}.exe
    )
elseif(APPLE)

elseif(LINUX)

endif()
