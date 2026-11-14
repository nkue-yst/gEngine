add_executable(gengine_runner WIN32 main.cpp)
target_link_libraries(gengine_runner PRIVATE gengine_play_window shell32)
set_target_properties(gengine_runner PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
gengine_use_components(gengine_runner)
get_directory_property(runner_rust_output DIRECTORY "${PROJECT_SOURCE_DIR}/components/gLang" DEFINITION GLANG_RUST_OUTPUT)
add_custom_target(gengine_game_module
    COMMAND "${runner_rust_output}/glangc.exe" "${PROJECT_SOURCE_DIR}/samples/shooter2d/game.glang" -o "${CMAKE_BINARY_DIR}/$<CONFIG>/shooter2d.glbc"
    COMMAND "${runner_rust_output}/glangc.exe" "${PROJECT_SOURCE_DIR}/samples/shooter3d/game.glang" -o "${CMAKE_BINARY_DIR}/$<CONFIG>/shooter3d.glbc"
    DEPENDS gengine_editor_compiler VERBATIM)
add_dependencies(gengine_runner gengine_game_module)

# 同じ論理パスの通常ファイルと圧縮packを作る。Editorもこの素材をそのまま使う。
set(sample_content "${CMAKE_BINARY_DIR}/sample-content/$<CONFIG>")
add_custom_target(gengine_sample_content
    COMMAND ${CMAKE_COMMAND} -E make_directory "${sample_content}/scenes"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/samples/assets" "${sample_content}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/$<CONFIG>/shooter2d.glbc" "${CMAKE_BINARY_DIR}/$<CONFIG>/shooter3d.glbc" "${sample_content}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/samples/shooter2d/scene.json" "${sample_content}/scenes/shooter2d.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/samples/shooter3d/scene.json" "${sample_content}/scenes/shooter3d.json"
    COMMAND $<TARGET_FILE:bpack> pack "${CMAKE_BINARY_DIR}/$<CONFIG>/samples.bpack" --base "${sample_content}" --overwrite --compress "${sample_content}"
    DEPENDS gengine_game_module bpack VERBATIM)
