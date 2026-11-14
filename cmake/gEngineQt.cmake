include_guard(GLOBAL)

# QtはEditorとGUI試験だけが使う。RuntimeやGameRunnerへ依存を伝えない。
find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Widgets)
set(GENGINE_QT_SOURCE_ARCHIVE "" CACHE FILEPATH "Corresponding Qt Base 6.12.0 source archive for binary redistribution")
if(GENGINE_QT_SOURCE_ARCHIVE)
    file(SHA256 "${GENGINE_QT_SOURCE_ARCHIVE}" qt_source_hash)
    if(NOT qt_source_hash STREQUAL "a951bd163c7b80fc6b8c88d7668fb56abf91c152373e13c10666763238131307")
        message(FATAL_ERROR "Qt 6.12.0 corresponding source checksum mismatch")
    endif()
endif()
function(gengine_use_qt target)
    # Rust側と共有する/MDを維持するため、Qtも同じCRTの配布バイナリーを両構成で使う。
    # 本体Debugの最適化・assert・PDBは変えず、QtのDebug CRTを同じプロセスへ混ぜない。
    foreach(qt_target Core Gui Widgets QWindowsIntegrationPlugin Test)
        if(TARGET Qt6::${qt_target})
            set_target_properties(Qt6::${qt_target} PROPERTIES MAP_IMPORTED_CONFIG_DEBUG RelWithDebInfo)
        endif()
    endforeach()
    target_link_libraries(${target} PRIVATE Qt6::Widgets)
    target_compile_definitions(${target} PRIVATE QT_NO_DEBUG)
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/Editor")
    add_custom_target(${target}_stage_qt
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/platforms" "$<TARGET_FILE_DIR:${target}>/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:${target}>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::Gui>" "$<TARGET_FILE_DIR:${target}>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::Widgets>" "$<TARGET_FILE_DIR:${target}>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>" "$<TARGET_FILE_DIR:${target}>/platforms/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/apps/Editor/qt.conf" "$<TARGET_FILE_DIR:${target}>/qt.conf"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/ThirdParty/Qt/NOTICES.txt" "$<TARGET_FILE_DIR:${target}>/licenses/Qt-NOTICES.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/ThirdParty/Qt/SOURCE.txt" "$<TARGET_FILE_DIR:${target}>/SOURCE.txt"
        VERBATIM)
    # 同じ配置先を使うGUI試験との同時コピーを避ける。
    get_property(previous_stage GLOBAL PROPERTY GENGINE_QT_STAGE)
    if(previous_stage)
        add_dependencies(${target}_stage_qt ${previous_stage})
    endif()
    set_property(GLOBAL PROPERTY GENGINE_QT_STAGE ${target}_stage_qt)
    add_dependencies(${target} ${target}_stage_qt)
endfunction()
