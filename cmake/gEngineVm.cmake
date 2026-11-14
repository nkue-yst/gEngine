include_guard(GLOBAL)

# gLangは独立部品として採用する。ビルド定義を本体へ複製しない。
set(GLANG_BUILD_TESTS ${GENGINE_BUILD_TESTS})
add_subdirectory(components/gLang)
