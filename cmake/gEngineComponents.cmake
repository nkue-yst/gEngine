include_guard(GLOBAL)

# 同じ実行先へ静的Runtimeをリンクし、起動時に選ぶバックエンドDLLを配置する。
function(gengine_use_components target)
    target_link_libraries(${target} PRIVATE aEngine::Runtime rEngine::Runtime)

    # DLLだけの更新でも配置を実行する。実行ファイルの再リンクを配置条件にしない。
    add_custom_target(${target}_stage_components
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${target}>/backends" "$<TARGET_FILE_DIR:${target}>/ThirdParty"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:aengine_backend_xaudio2>" "$<TARGET_FILE_DIR:${target}>/backends/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:rengine_backend_d3d12>" "$<TARGET_FILE_DIR:${target}>/backends/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../components/aEngine/LICENSE"
            "$<TARGET_FILE_DIR:${target}>/ThirdParty/aEngine-LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../components/rEngine/LICENSE"
            "$<TARGET_FILE_DIR:${target}>/ThirdParty/rEngine-LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../components/BinPackLib/LICENSE"
            "$<TARGET_FILE_DIR:${target}>/ThirdParty/BinPackLib-LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../components/gLang/LICENSE"
            "$<TARGET_FILE_DIR:${target}>/ThirdParty/gLang-LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_BINARY_DIR}/sample-content/$<CONFIG>" "$<TARGET_FILE_DIR:${target}>/content"
        DEPENDS aengine_backend_xaudio2 rengine_backend_d3d12 gengine_sample_content
        VERBATIM
    )
    # 同じ出力先を共有する実行物が、並列ビルドで同じDLLを書き換えないよう配置を直列化する。
    get_property(previous_stage GLOBAL PROPERTY GENGINE_COMPONENT_STAGE)
    if(previous_stage)
        add_dependencies(${target}_stage_components ${previous_stage})
    endif()
    set_property(GLOBAL PROPERTY GENGINE_COMPONENT_STAGE ${target}_stage_components)
    add_dependencies(${target} ${target}_stage_components)
endfunction()
