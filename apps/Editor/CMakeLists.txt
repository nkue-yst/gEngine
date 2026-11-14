# gLangの採用ソースを使い、既存の構成別Cargoキャッシュへコンパイラーを構築する。
get_directory_property(editor_cargo DIRECTORY "${PROJECT_SOURCE_DIR}/components/gLang" DEFINITION GLANG_CARGO)
get_directory_property(editor_cargo_directory DIRECTORY "${PROJECT_SOURCE_DIR}/components/gLang" DEFINITION GLANG_CARGO_DIRECTORY)
get_directory_property(editor_rust_output DIRECTORY "${PROJECT_SOURCE_DIR}/components/gLang" DEFINITION GLANG_RUST_OUTPUT)
add_custom_target(gengine_editor_compiler
    COMMAND "${editor_cargo}" build -p glang-compiler --bin glangc --locked
        --target x86_64-pc-windows-msvc --target-dir "${editor_cargo_directory}"
        "$<$<NOT:$<CONFIG:Debug>>:--release>"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/components/gLang"
    COMMAND_EXPAND_LISTS VERBATIM)
add_dependencies(gengine_editor_compiler glang_rust_runtime)

# GUIは共通コアへ接続し、実行物とコンパイラーを構成別の同じ場所へ配置する。
add_executable(gengine_editor WIN32 main.cpp)
target_link_libraries(gengine_editor PRIVATE gengine_editor_core comctl32 comdlg32 shell32 ole32)
target_compile_options(gengine_editor PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
gengine_use_qt(gengine_editor)

# コンパイラーだけの更新も配置する。Editorの再リンクには依存しない。
add_custom_target(gengine_editor_stage
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:gengine_editor>/ThirdParty"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${editor_rust_output}/glangc.exe" "$<TARGET_FILE_DIR:gengine_editor>/glangc.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/ThirdParty/nlohmann/LICENSE.MIT" "$<TARGET_FILE_DIR:gengine_editor>/ThirdParty/nlohmann-json-LICENSE.MIT"
    DEPENDS gengine_editor_compiler VERBATIM)
add_dependencies(gengine_editor gengine_editor_stage)

if(GENGINE_QT_SOURCE_ARCHIVE)
    add_custom_command(TARGET gengine_editor_stage POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:gengine_editor>/Sources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GENGINE_QT_SOURCE_ARCHIVE}" "$<TARGET_FILE_DIR:gengine_editor>/Sources/qtbase-everywhere-src-6.12.0.tar.xz"
        VERBATIM)
endif()
