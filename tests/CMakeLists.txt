add_executable(gengine_scene_document_test scene_document_test.cpp)
add_executable(gengine_display_test display_test.cpp)
target_link_libraries(gengine_display_test PRIVATE gengine_play_window)
target_compile_options(gengine_display_test PRIVATE /W4 /WX /utf-8)
gengine_use_components(gengine_display_test)
add_test(NAME gengine_display_test COMMAND gengine_display_test "$<TARGET_FILE_DIR:gengine_display_test>/backends")
set_tests_properties(gengine_display_test PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE LABELS "display;desktop;gpu")
add_executable(gengine_performance_test performance_test.cpp)
add_executable(gengine_performance_play_test performance_play_test.cpp)
target_link_libraries(gengine_performance_play_test PRIVATE gengine_play_window ole32)
target_compile_options(gengine_performance_play_test PRIVATE /W4 /WX /utf-8)
gengine_use_components(gengine_performance_play_test)
foreach(dimension IN ITEMS 2d 3d)
    add_test(NAME gengine_performance_play_${dimension}_test COMMAND gengine_performance_play_test
        "${CMAKE_BINARY_DIR}/$<CONFIG>/content" "${CMAKE_BINARY_DIR}/performance-play/$<CONFIG>/${dimension}" "${dimension}")
    set_tests_properties(gengine_performance_play_${dimension}_test PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE LABELS "performance;game;desktop")
endforeach()
target_link_libraries(gengine_performance_test PRIVATE gengine_performance)
add_test(NAME gengine_performance_test COMMAND gengine_performance_test)
add_executable(gengine_update_benchmark measure_update.cpp)
target_link_libraries(gengine_update_benchmark PRIVATE gengine_performance gVM::Runtime)
target_include_directories(gengine_update_benchmark SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/ThirdParty")
target_compile_options(gengine_update_benchmark PRIVATE /W4 /WX /utf-8 /fp:strict)
# 試験入力はEditorへの配置を待たず、ビルドしたコンパイラーで生成する。
get_directory_property(editor_test_rust_output DIRECTORY "${PROJECT_SOURCE_DIR}/components/gLang" DEFINITION GLANG_RUST_OUTPUT)
foreach(dimension IN ITEMS 2d 3d)
    set(update_module "${CMAKE_BINARY_DIR}/fixtures/$<CONFIG>/measure-${dimension}.glbc")
    add_custom_command(OUTPUT "${update_module}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/fixtures/$<CONFIG>"
        COMMAND "${editor_test_rust_output}/glangc.exe"
            "${PROJECT_SOURCE_DIR}/components/gLang/samples/language/bullets${dimension}.glang" -o "${update_module}"
        DEPENDS gengine_editor_compiler "${PROJECT_SOURCE_DIR}/components/gLang/samples/language/bullets${dimension}.glang" VERBATIM)
    add_custom_target(gengine_measure_${dimension} DEPENDS "${update_module}")
    add_dependencies(gengine_update_benchmark gengine_measure_${dimension})
    add_test(NAME gengine_update_${dimension}_test COMMAND gengine_update_benchmark "${update_module}" "${dimension}" smoke)
endforeach()
add_executable(gengine_scene2d_test scene2d_test.cpp)
target_link_libraries(gengine_scene2d_test PRIVATE gengine_scene)
target_compile_options(gengine_scene2d_test PRIVATE /W4 /WX /utf-8)
add_test(NAME gengine_scene2d_test COMMAND gengine_scene2d_test)
target_link_libraries(gengine_scene_document_test PRIVATE gengine_scene gengine_editor_core)
target_compile_options(gengine_scene_document_test PRIVATE /W4 /WX /utf-8)
add_test(NAME gengine_scene_document_test COMMAND gengine_scene_document_test "${PROJECT_SOURCE_DIR}")

add_executable(gengine_game_test game_test.cpp)
add_executable(gengine_actor_test actor_test.cpp)
target_link_libraries(gengine_actor_test PRIVATE gengine_game)
target_compile_options(gengine_actor_test PRIVATE /W4 /WX /utf-8)
add_dependencies(gengine_actor_test gengine_game_module)
add_test(NAME gengine_actor_test COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/actor_test.py"
    "$<TARGET_FILE_DIR:gengine_editor>/glangc.exe" "$<TARGET_FILE:gengine_actor_test>"
    "${CMAKE_BINARY_DIR}/actor-fixtures/$<CONFIG>")
add_executable(gengine_interaction_test interaction_test.cpp)
target_link_libraries(gengine_interaction_test PRIVATE gengine_game)
target_compile_options(gengine_interaction_test PRIVATE /W4 /WX /utf-8)
add_dependencies(gengine_interaction_test gengine_game_module)
add_test(NAME gengine_interaction_test COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/interaction_test.py"
    "$<TARGET_FILE_DIR:gengine_editor>/glangc.exe" "$<TARGET_FILE:gengine_interaction_test>"
    "${PROJECT_SOURCE_DIR}/samples/shooter2d/game.glang" "${CMAKE_BINARY_DIR}/interaction-fixtures/$<CONFIG>")
add_executable(gengine_play_window_test play_window_test.cpp)
target_link_libraries(gengine_play_window_test PRIVATE gengine_play_window ole32)
gengine_use_components(gengine_play_window_test)
add_dependencies(gengine_play_window_test gengine_game_module)
add_test(NAME gengine_play_window_test COMMAND gengine_play_window_test "${CMAKE_BINARY_DIR}/$<CONFIG>/shooter2d.glbc" "${CMAKE_BINARY_DIR}/play-validation/$<CONFIG>")
set_tests_properties(gengine_play_window_test PROPERTIES TIMEOUT 40 RUN_SERIAL TRUE LABELS "game;desktop;gpu;audio-device")
target_link_libraries(gengine_game_test PRIVATE gengine_game)
add_dependencies(gengine_game_test gengine_game_module)
add_test(NAME gengine_game_test COMMAND gengine_game_test "${CMAKE_BINARY_DIR}/$<CONFIG>/shooter2d.glbc")
add_test(NAME gengine_game_3d_test COMMAND gengine_game_test "${CMAKE_BINARY_DIR}/$<CONFIG>/shooter3d.glbc" 3d)
add_test(NAME gengine_play_window_3d_test COMMAND gengine_play_window_test "${CMAKE_BINARY_DIR}/$<CONFIG>/shooter3d.glbc" "${CMAKE_BINARY_DIR}/play-validation-3d/$<CONFIG>" 3d)
set_tests_properties(gengine_play_window_3d_test PROPERTIES TIMEOUT 40 RUN_SERIAL TRUE LABELS "game;desktop;gpu;audio-device")
add_executable(gengine_scene3d_test scene3d_test.cpp)
target_link_libraries(gengine_scene3d_test PRIVATE gengine_play_window rEngine::Runtime)
gengine_use_components(gengine_scene3d_test)
add_test(NAME gengine_scene3d_test COMMAND gengine_scene3d_test)
set_tests_properties(gengine_scene3d_test PROPERTIES TIMEOUT 20 LABELS "game;gpu")

add_executable(gengine_component_integration_test component_integration_test.cpp)
target_compile_options(gengine_component_integration_test PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
gengine_use_components(gengine_component_integration_test)

# 本体との接続だけを扱う。言語単独の試験はgLang側が所有する。
add_test(NAME gengine_component_integration_test COMMAND gengine_component_integration_test)
set_tests_properties(gengine_component_integration_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 30 LABELS "integration;gpu;audio-device")

add_executable(gengine_actions_test actions_test.cpp)
target_compile_options(gengine_actions_test PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
target_link_libraries(gengine_actions_test PRIVATE gEngine::Actions)
add_dependencies(gengine_actions_test glang_vm_fixtures)
get_target_property(fixtures glang_vm_fixtures GLANG_FIXTURE_DIRECTORY)
add_test(NAME gengine_actions_test COMMAND gengine_actions_test "${fixtures}/embedding.glbc")
set_tests_properties(gengine_actions_test PROPERTIES TIMEOUT 30 LABELS "actions;headless")

# 共通コアを画面なしで試験する。取消し用の遅い子プロセスは製品には配置しない。
add_executable(gengine_slow_compiler slow_compiler.cpp)
add_executable(gengine_build_test build_test.cpp)
target_link_libraries(gengine_build_test PRIVATE gengine_editor_core)
target_compile_options(gengine_build_test PRIVATE /W4 /WX /utf-8)
add_dependencies(gengine_build_test gengine_build_tools gengine_slow_compiler gengine_editor)
add_test(NAME gengine_build_test COMMAND gengine_build_test
    "${CMAKE_BINARY_DIR}/build-tests/$<CONFIG>" "$<TARGET_FILE_DIR:gengine_editor>" "$<TARGET_FILE:gengine_slow_compiler>")
set_tests_properties(gengine_build_test PROPERTIES TIMEOUT 100 LABELS "editor;build;headless")
target_compile_options(gengine_slow_compiler PRIVATE /W4 /WX /utf-8)
add_executable(gengine_editor_test editor_test.cpp)
target_link_libraries(gengine_editor_test PRIVATE gengine_editor_core)
target_compile_options(gengine_editor_test PRIVATE /W4 /WX /permissive- /utf-8)
add_dependencies(gengine_editor_test glang_rust_tools gengine_slow_compiler)
add_test(NAME gengine_editor_test COMMAND gengine_editor_test
    "${CMAKE_BINARY_DIR}/editor-tests/$<CONFIG>" "${editor_test_rust_output}/glangc.exe" "$<TARGET_FILE:gengine_slow_compiler>")
set_tests_properties(gengine_editor_test PROPERTIES TIMEOUT 45 LABELS "editor;headless;mcp")

# 幅1の既存CLIと同じ入力を使い、本体側の集合コピー・予算・メモリ報告まで検証する。
set(batch_fixture "${CMAKE_BINARY_DIR}/fixtures/$<CONFIG>/batch.glbc")
add_custom_command(OUTPUT "${batch_fixture}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/fixtures/$<CONFIG>"
    COMMAND "${editor_test_rust_output}/glangc.exe" "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/batch.glang" -o "${batch_fixture}"
    DEPENDS glang_rust_tools "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/batch.glang" VERBATIM)
add_custom_target(gengine_batch_fixture DEPENDS "${batch_fixture}")
add_executable(gengine_batch_host_test batch_host_test.cpp)
target_compile_options(gengine_batch_host_test PRIVATE /W4 /WX /permissive- /utf-8)
target_link_libraries(gengine_batch_host_test PRIVATE gEngine::Actions)
add_dependencies(gengine_batch_host_test gengine_batch_fixture)
add_test(NAME gengine_batch_parity_test COMMAND ${CMAKE_COMMAND}
    "-DHOST=$<TARGET_FILE:gengine_batch_host_test>" "-DRUNNER=${editor_test_rust_output}/gvm.exe"
    "-DMODULE=${batch_fixture}" -P "${CMAKE_CURRENT_SOURCE_DIR}/batch_parity.cmake")
set_tests_properties(gengine_batch_parity_test PROPERTIES TIMEOUT 60 LABELS "editor;headless;vm;metrics")

# 実Editorへのstdio接続はデスクトップを使うため、他の試験と同時実行しない。
if(TARGET gengine_editor)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    foreach(dimension IN ITEMS 2d 3d)
        add_test(NAME gengine_runner_performance_${dimension}_test COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/runner_performance_test.py" "$<TARGET_FILE:gengine_runner>"
            "${CMAKE_BINARY_DIR}/runner-performance/$<CONFIG>" "${dimension}")
        set_tests_properties(gengine_runner_performance_${dimension}_test PROPERTIES TIMEOUT 40 RUN_SERIAL TRUE LABELS "performance;game;desktop")
    endforeach()
    add_test(NAME gengine_build_mcp_test COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build_mcp_test.py"
        "$<TARGET_FILE:gengine_editor>" "${CMAKE_BINARY_DIR}/build-mcp-tests/$<CONFIG>" "$<TARGET_FILE:gengine_play_window_test>")
    set_tests_properties(gengine_build_mcp_test PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE LABELS "editor;build;mcp;desktop;game")
    add_test(NAME gengine_play_mcp_test COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/play_mcp_test.py"
        "$<TARGET_FILE:gengine_editor>" "${CMAKE_BINARY_DIR}/play-mcp-tests/$<CONFIG>" "${PROJECT_SOURCE_DIR}/samples/shooter2d/game.glang")
    set_tests_properties(gengine_play_mcp_test PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE LABELS "game;editor;mcp;desktop")
    add_test(NAME gengine_play_mcp_3d_test COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/play_mcp_test.py"
        "$<TARGET_FILE:gengine_editor>" "${CMAKE_BINARY_DIR}/play-mcp-tests-3d/$<CONFIG>" "${PROJECT_SOURCE_DIR}/samples/shooter3d/game.glang" 3d)
    set_tests_properties(gengine_play_mcp_3d_test PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE LABELS "game;editor;mcp;desktop")
    add_test(NAME gengine_editor_mcp_test COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/editor_mcp_test.py"
        "$<TARGET_FILE:gengine_editor>" "${CMAKE_BINARY_DIR}/editor-mcp-tests/$<CONFIG>")
    set_tests_properties(gengine_editor_mcp_test PROPERTIES TIMEOUT 60 LABELS "editor;mcp;desktop" RUN_SERIAL TRUE)
endif()

add_executable(gengine_scene_actions_test scene_actions_test.cpp)
add_executable(gengine_viewport_test viewport_test.cpp)
add_executable(gengine_viewport3d_test viewport3d_test.cpp)
target_link_libraries(gengine_viewport3d_test PRIVATE gengine_editor_core rEngine::Runtime)
target_compile_options(gengine_viewport3d_test PRIVATE /W4 /WX /utf-8)
add_test(NAME gengine_viewport3d_test COMMAND gengine_viewport3d_test "${CMAKE_BINARY_DIR}/viewport3d-tests/$<CONFIG>" "${editor_test_rust_output}/glangc.exe")
set_tests_properties(gengine_viewport3d_test PROPERTIES TIMEOUT 30 LABELS "editor;headless;scene")
add_executable(gengine_qt_host_test qt_host_test.cpp)
target_link_libraries(gengine_qt_host_test PRIVATE gengine_editor_core psapi)
target_compile_options(gengine_qt_host_test PRIVATE /W4 /WX /utf-8)
gengine_use_qt(gengine_qt_host_test)
gengine_use_components(gengine_qt_host_test)
add_dependencies(gengine_qt_host_test gengine_editor_compiler)
add_custom_command(TARGET gengine_qt_host_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${editor_test_rust_output}/glangc.exe" "$<TARGET_FILE_DIR:gengine_qt_host_test>/glangc.exe")
add_test(NAME gengine_qt_host_test COMMAND gengine_qt_host_test
    "${CMAKE_BINARY_DIR}/qt-host-tests/$<CONFIG>" "$<TARGET_FILE_DIR:gengine_qt_host_test>/glangc.exe")
set_tests_properties(gengine_qt_host_test PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE LABELS "editor;desktop;qt")
target_link_libraries(gengine_viewport_test PRIVATE gengine_editor_core)
target_compile_options(gengine_viewport_test PRIVATE /W4 /WX /utf-8)
add_test(NAME gengine_viewport_test COMMAND gengine_viewport_test "${CMAKE_BINARY_DIR}/viewport-tests/$<CONFIG>" "${editor_test_rust_output}/glangc.exe")
target_link_libraries(gengine_scene_actions_test PRIVATE gengine_editor_core advapi32)
target_compile_options(gengine_scene_actions_test PRIVATE /W4 /WX /utf-8)
add_test(NAME gengine_scene_actions_test COMMAND gengine_scene_actions_test "${CMAKE_BINARY_DIR}/scene-actions-tests/$<CONFIG>" "${editor_test_rust_output}/glangc.exe")
set_tests_properties(gengine_scene_actions_test PROPERTIES TIMEOUT 60 LABELS "editor;headless;scene")

if(TARGET gengine_editor)
    add_test(NAME gengine_scene_mcp_test COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scene_mcp_test.py"
        "$<TARGET_FILE:gengine_editor>" "${CMAKE_BINARY_DIR}/scene-mcp-tests/$<CONFIG>" "$<TARGET_FILE:gengine_runner>")
    set_tests_properties(gengine_scene_mcp_test PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE LABELS "scene;editor;mcp;desktop")
    add_test(NAME gengine_viewport_mcp_test COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/viewport_mcp_test.py"
        "$<TARGET_FILE:gengine_editor>" "${CMAKE_BINARY_DIR}/viewport-mcp-tests/$<CONFIG>")
    set_tests_properties(gengine_viewport_mcp_test PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE LABELS "scene;editor;mcp;desktop")
    add_test(NAME gengine_viewport3d_mcp_test COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/viewport3d_mcp_test.py"
        "$<TARGET_FILE:gengine_editor>" "${CMAKE_BINARY_DIR}/viewport3d-mcp-tests/$<CONFIG>")
    set_tests_properties(gengine_viewport3d_mcp_test PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE LABELS "scene;editor;mcp;desktop")
endif()

find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(gengine_qt_workflow_test qt_workflow_test.cpp)
target_link_libraries(gengine_qt_workflow_test PRIVATE gengine_editor_core Qt6::Test comctl32 shell32 ole32)
target_compile_options(gengine_qt_workflow_test PRIVATE /W4 /WX /utf-8)
gengine_use_qt(gengine_qt_workflow_test)
add_dependencies(gengine_qt_workflow_test gengine_distribution_vm)
add_custom_command(TARGET gengine_qt_workflow_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:gengine_qt_workflow_test>/")
if(GENGINE_QT_SOURCE_ARCHIVE)
    add_test(NAME gengine_distribution_test COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/distribution_test.py" "${CMAKE_BINARY_DIR}/distribution-$<CONFIG>.json"
        "${CMAKE_BINARY_DIR}/distribution-tests/$<CONFIG>" "$<TARGET_FILE:gengine_qt_workflow_test>")
    set_tests_properties(gengine_distribution_test PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE LABELS "deployment;editor;desktop;game;mcp")
endif()
add_executable(gengine_qt_performance_test qt_performance_test.cpp)
target_link_libraries(gengine_qt_performance_test PRIVATE gengine_editor_core Qt6::Test comctl32 shell32 ole32)
target_compile_options(gengine_qt_performance_test PRIVATE /W4 /WX /utf-8)
gengine_use_qt(gengine_qt_performance_test)
gengine_use_components(gengine_qt_performance_test)
add_dependencies(gengine_qt_performance_test gengine_build_tools gengine_editor_compiler)
add_custom_command(TARGET gengine_qt_performance_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:gengine_qt_performance_test>/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${editor_test_rust_output}/glangc.exe" "$<TARGET_FILE_DIR:gengine_qt_performance_test>/glangc.exe")
add_test(NAME gengine_qt_performance_test COMMAND gengine_qt_performance_test
    "${CMAKE_BINARY_DIR}/qt-performance/$<CONFIG>" "$<TARGET_FILE_DIR:gengine_qt_performance_test>/glangc.exe")
set_tests_properties(gengine_qt_performance_test PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE LABELS "performance;editor;desktop;qt")
add_executable(gengine_assets_test assets_test.cpp)
target_link_libraries(gengine_assets_test PRIVATE gengine_play_window psapi)
target_compile_options(gengine_assets_test PRIVATE /W4 /WX /utf-8)
gengine_use_components(gengine_assets_test)
add_test(NAME gengine_assets_test COMMAND gengine_assets_test
    "${CMAKE_BINARY_DIR}/$<CONFIG>/content" "${CMAKE_BINARY_DIR}/$<CONFIG>/samples.bpack"
    "${CMAKE_BINARY_DIR}/asset-tests/$<CONFIG>")
set_tests_properties(gengine_assets_test PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE LABELS "assets;game;gpu;audio-device")
foreach(dimension IN ITEMS 2d 3d)
    add_test(NAME gengine_pack_play_${dimension}_test COMMAND gengine_play_window_test
        "${CMAKE_BINARY_DIR}/$<CONFIG>/shooter${dimension}.glbc"
        "${CMAKE_BINARY_DIR}/pack-play-${dimension}/$<CONFIG>" "${dimension}"
        "${CMAKE_BINARY_DIR}/$<CONFIG>/samples.bpack")
    set_tests_properties(gengine_pack_play_${dimension}_test PROPERTIES TIMEOUT 40 RUN_SERIAL TRUE LABELS "assets;game;desktop")
endforeach()
add_test(NAME gengine_runner_assets_test COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/runner_assets_test.py" "$<TARGET_FILE:gengine_runner>"
    "${CMAKE_BINARY_DIR}/$<CONFIG>/samples.bpack" "${CMAKE_BINARY_DIR}/runner-asset-tests/$<CONFIG>")
set_tests_properties(gengine_runner_assets_test PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE LABELS "assets;game;deployment")
add_executable(gengine_qt_editor_test qt_editor_test.cpp)
target_link_libraries(gengine_qt_editor_test PRIVATE gengine_editor_core Qt6::Test comctl32 shell32 ole32 psapi)
target_compile_options(gengine_qt_editor_test PRIVATE /W4 /WX /utf-8)
gengine_use_qt(gengine_qt_editor_test)
gengine_use_components(gengine_qt_editor_test)
add_dependencies(gengine_qt_editor_test gengine_editor_compiler)
add_custom_command(TARGET gengine_qt_editor_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:gengine_qt_editor_test>/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${editor_test_rust_output}/glangc.exe" "$<TARGET_FILE_DIR:gengine_qt_editor_test>/glangc.exe")
add_test(NAME gengine_qt_editor_test COMMAND gengine_qt_editor_test "${CMAKE_BINARY_DIR}/qt-editor-tests/$<CONFIG>" "$<TARGET_FILE_DIR:gengine_qt_editor_test>/glangc.exe")
set_tests_properties(gengine_qt_editor_test PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE LABELS "editor;desktop;qt")
add_test(NAME gengine_qt_editor_hidpi_test COMMAND gengine_qt_editor_test "${CMAKE_BINARY_DIR}/qt-editor-tests/$<CONFIG>-hidpi" "$<TARGET_FILE_DIR:gengine_qt_editor_test>/glangc.exe")
set_tests_properties(gengine_qt_editor_hidpi_test PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE LABELS "editor;desktop;qt" ENVIRONMENT "QT_SCALE_FACTOR=1.5")
if(GENGINE_QT_SOURCE_ARCHIVE)
    add_test(NAME gengine_qt_deployment_test COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/qt_deployment_test.py"
        "$<TARGET_FILE:gengine_editor>" "${CMAKE_BINARY_DIR}/qt-deployment-tests/$<CONFIG>" "$<CONFIG>")
    set_tests_properties(gengine_qt_deployment_test PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE LABELS "editor;desktop;qt;deployment")
endif()

# 素材登録はGPU・音声デバイスを作らず、保存と共通デコーダーの失敗境界を確認する。
add_executable(gengine_asset_actions_test asset_actions_test.cpp)
target_link_libraries(gengine_asset_actions_test PRIVATE gengine_editor_core)
target_compile_options(gengine_asset_actions_test PRIVATE /W4 /WX /utf-8)
add_test(NAME gengine_asset_actions_test COMMAND gengine_asset_actions_test
    "${CMAKE_BINARY_DIR}/asset-actions-tests/$<CONFIG>" "${editor_test_rust_output}/glangc.exe"
    "${CMAKE_BINARY_DIR}/$<CONFIG>/content")
set_tests_properties(gengine_asset_actions_test PROPERTIES TIMEOUT 30 LABELS "assets;editor;headless")

add_test(NAME gengine_asset_mcp_test COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/asset_mcp_test.py"
    "$<TARGET_FILE:gengine_editor>" "${CMAKE_BINARY_DIR}/asset-mcp-tests/$<CONFIG>")
set_tests_properties(gengine_asset_mcp_test PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE LABELS "assets;editor;mcp;desktop")

add_executable(gengine_asset_references_test asset_references_test.cpp)
target_link_libraries(gengine_asset_references_test PRIVATE gengine_play_window psapi)
target_compile_options(gengine_asset_references_test PRIVATE /W4 /WX /utf-8)
gengine_use_components(gengine_asset_references_test)
set(reference_output "${CMAKE_BINARY_DIR}/asset-reference-tests/$<CONFIG>")
add_test(NAME gengine_asset_references_test COMMAND gengine_asset_references_test
    "${CMAKE_BINARY_DIR}/$<CONFIG>/content" "${reference_output}")
set_tests_properties(gengine_asset_references_test PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE
    LABELS "assets;game;gpu;audio-device" FIXTURES_SETUP assigned_assets)
foreach(dimension IN ITEMS 2d 3d)
    add_test(NAME gengine_reference_pack_play_${dimension}_test COMMAND gengine_play_window_test
        "${CMAKE_BINARY_DIR}/$<CONFIG>/shooter${dimension}.glbc"
        "${reference_output}/pack-${dimension}" "${dimension}" "${reference_output}/references.bpack")
    add_test(NAME gengine_reference_native_play_${dimension}_test COMMAND gengine_play_window_test
        "${CMAKE_BINARY_DIR}/$<CONFIG>/shooter${dimension}.glbc"
        "${reference_output}/native-${dimension}" "${dimension}" "${reference_output}/content" native)
    set_tests_properties(gengine_reference_pack_play_${dimension}_test gengine_reference_native_play_${dimension}_test
        PROPERTIES TIMEOUT 40 RUN_SERIAL TRUE FIXTURES_REQUIRED assigned_assets LABELS "assets;game;desktop")
endforeach()

add_executable(gengine_profile_store_test profile_store_test.cpp)
target_link_libraries(gengine_profile_store_test PRIVATE gengine_profile)
target_compile_definitions(gengine_profile_store_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(gengine_profile_store_test PRIVATE /W4 /WX /utf-8)
add_test(NAME gengine_profile_store_test COMMAND gengine_profile_store_test "${CMAKE_BINARY_DIR}/profile-store-tests/$<CONFIG>")
set_tests_properties(gengine_profile_store_test PROPERTIES TIMEOUT 20 LABELS "profile;headless")

add_executable(gengine_profile_test profile_test.cpp)
target_link_libraries(gengine_profile_test PRIVATE gengine_game)
target_compile_options(gengine_profile_test PRIVATE /W4 /WX /utf-8)
add_dependencies(gengine_profile_test gengine_game_module)
add_test(NAME gengine_profile_test COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/profile_test.py"
    "$<TARGET_FILE_DIR:gengine_editor>/glangc.exe" "$<TARGET_FILE:gengine_profile_test>"
    "${PROJECT_SOURCE_DIR}/samples/shooter2d/game.glang" "${CMAKE_BINARY_DIR}/profile-tests/$<CONFIG>")
set_tests_properties(gengine_profile_test PROPERTIES TIMEOUT 30 LABELS "profile;game;headless")

# aEngineの既存試験用実装で消失と初期化失敗を再現する。配布物には含めない。
foreach(fault IN ITEMS lost absent)
    add_library(gengine_audio_${fault}_backend MODULE "${PROJECT_SOURCE_DIR}/components/aEngine/tests/plugin/fixture.cpp")
    target_include_directories(gengine_audio_${fault}_backend PRIVATE "${PROJECT_SOURCE_DIR}/components/aEngine/include")
    target_compile_definitions(gengine_audio_${fault}_backend PRIVATE FIXTURE_ID="xaudio2")
endforeach()
target_compile_definitions(gengine_audio_lost_backend PRIVATE DEVICE_FAILURE)
target_compile_definitions(gengine_audio_absent_backend PRIVATE CREATE_FAILURE)
add_executable(gengine_audio_test game_audio_test.cpp)
target_link_libraries(gengine_audio_test PRIVATE gengine_play_window)
target_compile_options(gengine_audio_test PRIVATE /W4 /WX /utf-8)
gengine_use_components(gengine_audio_test)
add_dependencies(gengine_audio_test gengine_audio_lost_backend gengine_audio_absent_backend)
add_test(NAME gengine_audio_test COMMAND gengine_audio_test
    "${CMAKE_BINARY_DIR}/$<CONFIG>/content" "$<TARGET_FILE_DIR:gengine_audio_test>/backends"
    "$<TARGET_FILE:gengine_audio_lost_backend>" "$<TARGET_FILE:gengine_audio_absent_backend>"
    "${CMAKE_BINARY_DIR}/audio-tests/$<CONFIG>")
set_tests_properties(gengine_audio_test PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE LABELS "audio-device;game")

add_executable(gengine_input_test input_test.cpp)
target_link_libraries(gengine_input_test PRIVATE gengine_input)
target_compile_options(gengine_input_test PRIVATE /W4 /WX /utf-8)
add_test(NAME gengine_input_test COMMAND gengine_input_test)
set_tests_properties(gengine_input_test PROPERTIES TIMEOUT 15 LABELS "input;headless")

add_executable(gengine_game_definition_test game_definition_test.cpp)
target_link_libraries(gengine_game_definition_test PRIVATE gengine_assets)
target_compile_definitions(gengine_game_definition_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(gengine_game_definition_test PRIVATE /W4 /WX /utf-8)
add_dependencies(gengine_game_definition_test gengine_sample_content)
add_test(NAME gengine_game_definition_test COMMAND gengine_game_definition_test
    "${CMAKE_BINARY_DIR}/$<CONFIG>/content" "${CMAKE_BINARY_DIR}/definition-tests/$<CONFIG>")
set_tests_properties(gengine_game_definition_test PROPERTIES TIMEOUT 20 LABELS "assets;headless")

add_executable(gengine_product_test product_test.cpp)
target_link_libraries(gengine_product_test PRIVATE gengine_editor_core)
target_compile_options(gengine_product_test PRIVATE /W4 /WX /utf-8)
add_dependencies(gengine_product_test gengine_build_tools)
add_test(NAME gengine_product_test COMMAND gengine_product_test
    "$<TARGET_FILE_DIR:gengine_editor>" "${CMAKE_BINARY_DIR}/product-tests/$<CONFIG>")
set_tests_properties(gengine_product_test PROPERTIES TIMEOUT 20 LABELS "build;headless")

add_executable(gengine_frame_clock_test frame_clock_test.cpp)
target_link_libraries(gengine_frame_clock_test PRIVATE gengine_play_window)
target_compile_options(gengine_frame_clock_test PRIVATE /W4 /WX /utf-8)
add_test(NAME gengine_frame_clock_test COMMAND gengine_frame_clock_test)
set_tests_properties(gengine_frame_clock_test PROPERTIES TIMEOUT 15 RUN_SERIAL TRUE LABELS "performance;headless")

add_executable(gengine_visual_manifest_test visual_manifest_test.cpp)
target_link_libraries(gengine_visual_manifest_test PRIVATE gengine_assets)
target_include_directories(gengine_visual_manifest_test SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/ThirdParty")
target_compile_options(gengine_visual_manifest_test PRIVATE /W4 /WX /utf-8)
add_test(NAME gengine_visual_manifest_test COMMAND gengine_visual_manifest_test)
set_tests_properties(gengine_visual_manifest_test PROPERTIES TIMEOUT 15 LABELS "assets;headless")
