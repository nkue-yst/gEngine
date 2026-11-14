#include "assets.hpp"
#include <aengine/audio.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

namespace gengine
{
    namespace
    {
        void Require(bool condition, const char* message)
        {
            if (condition == false) {
                throw std::runtime_error(message);
            }
        }

        std::uint32_t Crc32(std::span<const std::uint8_t> bytes)
        {
            auto crc = std::uint32_t{0xffffffff};
            for (const auto byte : bytes) {
                crc ^= byte;
                for (auto bit = 0; bit < 8; ++bit) {
                    crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
                }
            }
            return crc ^ 0xffffffffu;
        }

        void CheckImage(HRESULT result)
        {
            Require(SUCCEEDED(result), "invalid or unsupported PNG image");
        }
    } // namespace

    std::unique_ptr<AssetSource> AssetSource::Reopen() const
    {
        return std::make_unique<AssetSource>(root_, packed_);
    }

    AssetSource::AssetSource(const std::filesystem::path& path, bool packed)
        : root_(std::filesystem::absolute(path).lexically_normal()), packed_(packed)
    {
        if (packed) {
            Require(std::filesystem::is_regular_file(root_) && std::filesystem::file_size(root_) <= 128 * 1024 * 1024,
                    "pack is missing or exceeds 128 MiB");
            auto files = std::make_unique<bin_pack_lib::PackedFileSystem>();
            Require(files->Open(root_.string()), "cannot open pack: missing, corrupt or unsupported format");
            files_ = std::move(files);
        }
        else {
            Require(std::filesystem::is_directory(root_), "asset directory is missing");
            files_ = std::make_unique<bin_pack_lib::NativeFileSystem>(root_.string());
        }
    }

    std::string AssetSource::ProviderPath(const std::string& path, bool allow_missing) const
    {
        // OSパスや親参照を許さず、通常ファイルではリンクによるルート外参照も拒否する。
        Require(path.empty() == false && path.front() != '/' && path.find_first_of("\\:*?\"<>|") == std::string::npos &&
                    path.find('\0') == std::string::npos,
                "expected a relative logical path");
        auto native = root_;
        for (auto start = std::size_t{0}; start <= path.size();) {
            const auto end = path.find('/', start);
            const auto part = path.substr(start, end == std::string::npos ? end : end - start);
            Require(part.empty() == false && part != "." && part != ".." && part.back() != '.' && part.back() != ' ',
                    "invalid logical path component");
            if (packed_ == false) {
                native /= std::filesystem::path(
                    std::u8string_view(reinterpret_cast<const char8_t*>(part.data()), part.size()));
                const auto attributes = GetFileAttributesW(native.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES) {
                    const auto error = GetLastError();
                    Require(allow_missing && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND),
                            "missing or inaccessible asset");
                }
                else {
                    Require((attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0, "linked asset");
                }
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }

        // gEngineの論理参照はUTF-8。BinPackLibの現行stringパス境界でネイティブ文字列へ変換する。
        return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size()))
            .string();
    }

    bool AssetSource::Exists(const std::string& path) const
    {
        try {
            return files_->Exists(ProviderPath(path, true));
        } catch (const std::exception& error) {
            throw std::runtime_error(path + ": " + error.what());
        }
    }

    std::vector<std::uint8_t> AssetSource::Read(const std::string& path, std::uint64_t limit,
                                                AssetMetrics& metrics) const
    {
        try {
            // OpenReadより先に展開後サイズを制限し、圧縮入力からの過大な確保を防ぐ。
            const auto provider_path = ProviderPath(path, false);
            Require(files_->Exists(provider_path), "missing asset");
            const auto info = files_->GetFileInfo(provider_path);
            Require(info.original_size > 0 && info.original_size <= limit && info.stored_size <= limit,
                    "asset size exceeds limit or is empty");
            const auto compressed = bin_pack_lib::HasFlag(info.flags, bin_pack_lib::FileFlags::kCompressed);
            auto stream = files_->OpenRead(provider_path);
            Require(stream != nullptr && stream->IsOpen() && stream->Size() == info.original_size,
                    "cannot read or decompress asset");
            auto bytes = std::vector<std::uint8_t>(static_cast<std::size_t>(info.original_size));
            auto offset = std::size_t{0};
            while (offset < bytes.size()) {
                const auto count = stream->Read(bytes.data() + offset, bytes.size() - offset);
                Require(count > 0 && count <= bytes.size() - offset, "incomplete asset read");
                offset += count;
            }
            if (packed_) {
                Require(Crc32(bytes) == info.crc32, "asset CRC32 mismatch");
            }

            // 圧縮データ+展開先、展開ストリーム+呼出し元コピーの大きい方を記録する。
            const auto peak =
                compressed ? std::max(info.stored_size + info.original_size, info.original_size + bytes.capacity())
                           : bytes.capacity();
            metrics.read_peak_bytes = std::max(metrics.read_peak_bytes, peak);
            metrics.peak_payload_bytes = std::max(metrics.peak_payload_bytes, peak + metrics.resident_pcm_bytes);
            ++metrics.files_read;
            metrics.compressed_files += compressed ? 1 : 0;
            return bytes;
        } catch (const std::exception& error) {
            throw std::runtime_error(path + ": " + error.what());
        }
    }

    std::string AssetKind(std::string_view path)
    {
        const auto dot = path.find_last_of('.');
        auto extension = std::string(dot == path.npos ? std::string_view{} : path.substr(dot));
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (extension == ".png") {
            return "image";
        }
        if (extension == ".gmesh") {
            return "mesh";
        }
        if (extension == ".wav") {
            return "audio";
        }
        throw std::invalid_argument("supported assets: .png, .gmesh, .wav");
    }

    AssetInfo InspectAsset(const AssetSource& source, const std::string& path, std::string_view expected_kind)
    {
        auto info = AssetInfo{};
        info.path = path;
        try {
            info.kind = AssetKind(path);
            if (expected_kind.empty() == false && info.kind != expected_kind) {
                throw std::invalid_argument("expected " + std::string(expected_kind) + " asset");
            }
            auto metrics = AssetMetrics{};
            const auto bytes = source.Read(path,
                                           info.kind == "image"  ? 16 * 1024 * 1024
                                           : info.kind == "mesh" ? 4 * 1024 * 1024
                                                                 : 1024 * 1024,
                                           metrics);
            info.bytes = bytes.size();
            // Runtimeと同じデコーダーを通す。検査後は画像・頂点・入力を解放する。
            if (info.kind == "image") {
                const auto image = DecodePng(bytes);
                info.summary = std::to_string(image.width) + " x " + std::to_string(image.height) + " RGBA8";
            }
            else if (info.kind == "mesh") {
                const auto mesh = DecodeMesh(bytes);
                info.summary = std::to_string(mesh.vertices.size()) + " vertices / " +
                               std::to_string(mesh.indices.size() / 3) + " triangles";
            }
            else {
                const auto pcm = aengine::InspectWav(std::as_bytes(std::span(bytes)));
                Require(pcm.has_value(), "invalid or unsupported PCM16 WAV");
                info.summary =
                    std::to_string(pcm->sample_rate) + " Hz / " + std::to_string(pcm->channels) + " ch / PCM16";
            }
        } catch (const std::exception& error) {
            info.diagnostic = error.what();
        }
        return info;
    }

    std::vector<AssetReference> InspectSceneAssets(const SceneDocument& scene, const AssetSource& source)
    {
        auto references = std::vector<AssetReference>{};
        for (const auto& object : scene.objects) {
            for (const auto visual : {true, false}) {
                const auto& path = visual ? object.visual : object.audio;
                if (path) {
                    const auto expected = visual ? (scene.game == "2d" ? "image" : "mesh") : "audio";
                    const auto info = InspectAsset(source, *path, expected);
                    references.push_back({object.id, visual ? "visual" : "audio", *path, info.diagnostic});
                }
            }
        }
        return references;
    }

    ImageData DecodePng(std::span<const std::uint8_t> bytes)
    {
        // 呼出し元がCOMを初期化済みでも使えるよう、成功した参照だけを対応して解放する。
        struct ComScope
        {
            HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            ~ComScope()
            {
                if (SUCCEEDED(result)) {
                    CoUninitialize();
                }
            }
        } com;
        Require(SUCCEEDED(com.result) || com.result == RPC_E_CHANGED_MODE, "cannot initialize image decoder");
        constexpr auto kSignature = std::array<std::uint8_t, 8>{137, 80, 78, 71, 13, 10, 26, 10};
        Require(bytes.size() >= 8 && bytes.size() <= 16 * 1024 * 1024 &&
                    std::equal(kSignature.begin(), kSignature.end(), bytes.begin()),
                "expected PNG image");
        // WICのメモリストリームは入力を借用する。コピー完了までbytesを生存させる。
        Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
        CheckImage(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
        Microsoft::WRL::ComPtr<IWICStream> stream;
        CheckImage(factory->CreateStream(&stream));
        CheckImage(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size())));
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        CheckImage(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder));
        auto frames = UINT{0};
        CheckImage(decoder->GetFrameCount(&frames));
        Require(frames == 1, "PNG must contain one frame");
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        CheckImage(decoder->GetFrame(0, &frame));
        auto result = ImageData{};
        CheckImage(frame->GetSize(&result.width, &result.height));
        // 横長・縦長のatlasを許可するが、展開後の最大16 MiBは従来のまま保つ。
        Require(result.width > 0 && result.height > 0 && result.width <= 4096 && result.height <= 4096 &&
                    std::uint64_t(result.width) * result.height <= 2048ull * 2048,
                "PNG dimensions exceed 4096 per axis or 4194304 pixels");
        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        CheckImage(factory->CreateFormatConverter(&converter));
        CheckImage(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                         WICBitmapPaletteTypeCustom));
        result.pixels.resize(std::size_t(result.width) * result.height * 4);
        CheckImage(converter->CopyPixels(nullptr, result.width * 4, static_cast<UINT>(result.pixels.size()),
                                         result.pixels.data()));
        return result;
    }

    MeshData DecodeMesh(std::span<const std::uint8_t> bytes)
    {
        // GMESH01はlittle-endianの固定レコード。C++構造体の配置に保存形式を依存させない。
        Require(bytes.size() >= 16 && std::memcmp(bytes.data(), "GMESH01\0", 8) == 0, "unsupported mesh format");
        auto offset = std::size_t{8};
        const auto u32 = [&]() {
            Require(offset + 4 <= bytes.size(), "truncated mesh");
            const auto value = std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
                               (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
            offset += 4;
            return value;
        };
        const auto vertex_count = u32();
        const auto index_count = u32();
        Require(vertex_count > 0 && vertex_count <= 65536 && index_count > 0 && index_count <= 393216 &&
                    index_count % 3 == 0 && bytes.size() == 16ull + vertex_count * 32ull + index_count * 4ull,
                "invalid mesh counts or length");
        auto result = MeshData{};
        result.vertices.resize(vertex_count);
        result.indices.resize(index_count);
        for (auto& vertex : result.vertices) {
            const auto number = [&]() {
                const auto value = std::bit_cast<float>(u32());
                Require(std::isfinite(value) && std::abs(value) <= 100000, "invalid mesh coordinate");
                return value;
            };
            for (auto& value : vertex.position) {
                value = number();
            }
            for (auto& value : vertex.normal) {
                value = number();
            }
            Require(std::hypot(vertex.normal[0], vertex.normal[1], vertex.normal[2]) > 0.001f, "mesh normal is zero");
            for (auto& value : vertex.uv) {
                value = number();
            }
            std::fill_n(vertex.color, 4, 1.0f);
        }
        for (auto& index : result.indices) {
            index = u32();
            Require(index < vertex_count, "mesh index exceeds vertex count");
        }
        return result;
    }

    GameInput LoadGameInput(const AssetSource& source, std::optional<bool> three_dimensional)
    {
        auto result = GameInput{};
        const auto name = std::string(three_dimensional.value_or(false) ? "shooter3d" : "shooter2d");
        // シーンを先に検証し、失敗時にVMや描画・音声の資源を作らない。
        auto scene_path = "scenes/" + name + ".json";
        auto module_path = name + ".glbc";
        const auto defined = source.Exists("game.json");
        if (defined) {
            const auto definition = DecodeGameDefinition(source.Read("game.json", 4096, result.metrics));
            result.title = definition.title;
            scene_path = definition.scene;
            module_path = definition.module;
        }
        try {
            const auto scene_bytes = source.Read(scene_path, 1024 * 1024, result.metrics);
            result.scene = ParseScene({reinterpret_cast<const char*>(scene_bytes.data()), scene_bytes.size()});
            ValidateSceneForPlay(result.scene);
            if (three_dimensional || defined == false) {
                Require(result.scene.game == (three_dimensional.value_or(false) ? "3d" : "2d"),
                        "scene dimension mismatch");
            }
        } catch (const std::exception& error) {
            throw std::runtime_error(scene_path + ": " + error.what());
        }
        result.module = source.Read(module_path, 64 * 1024 * 1024, result.metrics);
        return result;
    }
} // namespace gengine
