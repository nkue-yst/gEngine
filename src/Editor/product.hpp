#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace gengine::editor
{
    struct ProductDefinition
    {
        std::string title;
        std::string executable;
        std::string icon;
    };

    // product.jsonは制作時だけ使い、実行時の入口はgame.jsonに固定する。
    ProductDefinition DecodeProductDefinition(std::string_view text);
    void ValidateProductExecutable(std::string_view name);

    // 新規配置したRunnerのコピーへ、検証済みPNGを含むICOと製品識別を埋め込む。
    // 元のRunnerには適用しない。失敗したコピーの回収はBuildPipelineが所有する。
    void EmbedProductResources(const std::filesystem::path& executable, std::span<const std::uint8_t> icon);
} // namespace gengine::editor
