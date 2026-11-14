include_guard(GLOBAL)
find_package(Python3 3.11 COMPONENTS Interpreter REQUIRED)

# 単独VMは試験の有無にかかわらず配置する。部品の採用ソースとCargoキャッシュを共用する。
get_directory_property(distribution_cargo DIRECTORY "${PROJECT_SOURCE_DIR}/components/gLang" DEFINITION GLANG_CARGO)
get_directory_property(distribution_cache DIRECTORY "${PROJECT_SOURCE_DIR}/components/gLang" DEFINITION GLANG_CARGO_DIRECTORY)
get_directory_property(distribution_rust DIRECTORY "${PROJECT_SOURCE_DIR}/components/gLang" DEFINITION GLANG_RUST_OUTPUT)
add_custom_target(gengine_distribution_vm
    COMMAND "${distribution_cargo}" build -p gvm-runner --bin gvm --locked
        --target x86_64-pc-windows-msvc --target-dir "${distribution_cache}"
        "$<$<NOT:$<CONFIG:Debug>>:--release>"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/components/gLang"
    COMMAND_EXPAND_LISTS VERBATIM)
add_dependencies(gengine_distribution_vm gengine_editor)

# 入力の絶対パスはローカルのビルド設定だけに保持し、成果物には相対パスとハッシュを記録する。
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/distribution-$<CONFIG>.json" CONTENT
"{\n  \"source\": \"${PROJECT_SOURCE_DIR}\",\n  \"editor\": \"$<TARGET_FILE_DIR:gengine_editor>\",\n  \"gvm\": \"${distribution_rust}/gvm.exe\",\n  \"pack\": \"$<TARGET_FILE_DIR:gengine_runner>/samples.bpack\",\n  \"version\": \"${PROJECT_VERSION}\",\n  \"configuration\": \"$<CONFIG>\"\n}\n")
set(GENGINE_DISTRIBUTION_DIRECTORY "${CMAKE_BINARY_DIR}/../distribution" CACHE PATH "Parent directory for new local distributions")
add_custom_target(gengine_distribution
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/cmake/package_distribution.py"
        "${CMAKE_BINARY_DIR}/distribution-$<CONFIG>.json"
        "${GENGINE_DISTRIBUTION_DIRECTORY}/gEngine-${PROJECT_VERSION}-$<CONFIG>"
    DEPENDS gengine_distribution_vm VERBATIM)
