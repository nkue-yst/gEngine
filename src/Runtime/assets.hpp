#pragma once
#include "scene_document.hpp"
#include <BinPackLib/FileSystem.h>
#include <rengine/resources.hpp>
#include <span>

namespace gengine
{
    struct AssetMetrics
    {
        std::uint64_t files_read = 0;
        std::uint64_t compressed_files = 0;
        // BinPackLibの展開・読取りコピーが重なるpayload量。allocator等の内部領域は含めない。
        std::uint64_t read_peak_bytes = 0;
        std::uint64_t decode_peak_bytes = 0;
        std::uint64_t transfer_peak_bytes = 0;
        std::uint64_t resident_pcm_bytes = 0;
        std::uint64_t peak_payload_bytes = 0;
    };

    // ファイル操作は既存IFileSystemへ委譲する。本体は論理パス・上限・破損を検証する。
    class AssetSource final
    {
      public:
        explicit AssetSource(const std::filesystem::path& path, bool packed = false);
        // 構築元の寿命に依存せず、デバイス復旧時にも同じ入力を読み直す。
        std::unique_ptr<AssetSource> Reopen() const;
        bool Exists(const std::string& path) const;
        std::vector<std::uint8_t> Read(const std::string& path, std::uint64_t limit, AssetMetrics& metrics) const;

      private:
        std::string ProviderPath(const std::string& path, bool allow_missing) const;
        std::filesystem::path root_;
        bool packed_;
        std::unique_ptr<bin_pack_lib::IFileSystem> files_;
    };

    struct ImageData
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<std::uint8_t> pixels;
    };

    struct AudioManifest
    {
        std::vector<std::string> events;
        std::vector<std::string> tracks;
        std::uint32_t title_track = 0;
        std::uint32_t play_track = 0;
    };
    // Runtimeと配布ビルドで同じ音源定義を検証する。0は無音、曲番号は1始まり。
    AudioManifest DecodeAudioManifest(std::span<const std::uint8_t> bytes);
    std::string SerializeAudioManifest(const AudioManifest& manifest);
    struct MeshData
    {
        std::vector<rengine::Vertex> vertices;
        std::vector<std::uint32_t> indices;
    };
    // 一覧はデコード済みデータを保持せず、形式と小さな検査結果だけを持つ。
    struct AssetInfo
    {
        std::string path;
        std::string kind;
        std::uint64_t bytes = 0;
        std::string summary;
        std::string diagnostic;
    };
    std::string AssetKind(std::string_view path);
    AssetInfo InspectAsset(const AssetSource& source, const std::string& path, std::string_view expected_kind = {});
    struct AssetReference
    {
        std::string object_id;
        std::string slot;
        std::string path;
        std::string diagnostic;
    };
    // 文書は修正可能なまま保ち、対象と欄ごとに現在の参照エラーを返す。
    std::vector<AssetReference> InspectSceneAssets(const SceneDocument& scene, const AssetSource& source);

    struct GameInput
    {
        std::vector<std::uint8_t> module;
        SceneDocument scene;
        AssetMetrics metrics;
        std::string title;
    };

    struct GameDefinition
    {
        std::string title;
        std::string module;
        std::string scene;
    };
    // 配布時と実行時で同じ入口を検証し、サンプル固有の名前を製品に要求しない。
    GameDefinition DecodeGameDefinition(std::span<const std::uint8_t> bytes);
    std::string SerializeGameDefinition(const GameDefinition& definition);

    // 通常ファイルとpackは同じ形式検証・デコードを通す。戻り値が入力から独立して所有する。
    ImageData DecodePng(std::span<const std::uint8_t> bytes);
    MeshData DecodeMesh(std::span<const std::uint8_t> bytes);
    GameInput LoadGameInput(const AssetSource& source, std::optional<bool> three_dimensional = std::nullopt);
} // namespace gengine
