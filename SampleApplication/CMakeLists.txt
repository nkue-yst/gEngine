cmake_minimum_required(VERSION 3.16)

project(SampleApplication)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_CURRENT_SOURCE_DIR}/Binaries/Debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}/Binaries/Release)

add_executable(${PROJECT_NAME} SampleApplication.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../Runtime/Core/Main
)

set(GN_LIBRARIES
    GnClassLibrary
    GnCore
)

foreach(DLL IN LISTS GN_LIBRARIES)
    target_link_libraries(${PROJECT_NAME} ${DLL})

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/../Binaries/$<CONFIG>/${DLL}.dll $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
endforeach()


