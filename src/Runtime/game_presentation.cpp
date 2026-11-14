#include "game_presentation.hpp"
#include <algorithm>
#include <stdexcept>

namespace gengine
{
    namespace
    {
        template <class T> T Take(rengine::Expected<T> value)
        {
            if (value.has_value() == false) {
                throw std::runtime_error(value.error().message);
            }
            return std::move(*value);
        }

        struct Picture
        {
            rengine::Renderer& renderer;
            rengine::Texture texture{};
            rengine::Material material{};
            rengine::Instance instance{};
            std::uint32_t width = 0;
            std::uint32_t height = 0;

            explicit Picture(rengine::Renderer& value) : renderer(value)
            {
            }
            ~Picture()
            {
                // マテリアルの破棄結果にかかわらず、テクスチャの解放にも進む。
                if (material.id != 0) {
                    static_cast<void>(renderer.DestroyMaterial(material));
                }
                if (texture.id != 0) {
                    static_cast<void>(renderer.DestroyTexture(texture));
                }
            }

            rengine::Draw Draw() const
            {
                auto draw = rengine::Draw{};
                draw.kind = 1;
                draw.material = material;
                draw.instances = &instance;
                draw.instance_count = 1;
                return draw;
            }
        };

        std::unique_ptr<Picture> LoadPicture(rengine::Renderer& renderer, const AssetSource& source,
                                             const std::string& path, AssetMetrics& metrics)
        {
            if (path.empty()) {
                return {};
            }
            // 所有物を先に作り、途中のPNG・GPU作成失敗でも確保済み資源を回収する。
            auto result = std::make_unique<Picture>(renderer);
            const auto bytes = source.Read(path, 16 * 1024 * 1024, metrics);
            const auto image = DecodePng(bytes);
            result->width = image.width;
            result->height = image.height;
            metrics.decode_peak_bytes = std::max(metrics.decode_peak_bytes, std::uint64_t(image.pixels.capacity()));
            metrics.transfer_peak_bytes =
                std::max(metrics.transfer_peak_bytes, std::uint64_t(bytes.capacity() + image.pixels.capacity()));
            metrics.peak_payload_bytes = std::max(metrics.peak_payload_bytes, metrics.transfer_peak_bytes);
            result->texture =
                Take(renderer.CreateTexture({image.width, image.height, 0, image.pixels.data(), image.pixels.size()}));
            auto material = rengine::DefaultMaterial();
            material.texture = result->texture;
            material.blend = 1;
            result->material = Take(renderer.CreateMaterial(material));
            return result;
        }
    } // namespace

    struct GamePresentation::Impl
    {
        VisualManifest definition;
        std::unique_ptr<Picture> backdrop;
        std::unique_ptr<Picture> logo;
        std::unique_ptr<Picture> atlas;
        std::unique_ptr<Picture> detail;
        std::unique_ptr<Picture> environment;
    };

    GamePresentation::GamePresentation(rengine::Renderer& renderer, const AssetSource& source, AssetMetrics& metrics)
        : impl_(std::make_unique<Impl>())
    {
        impl_->definition = DecodeVisualManifest(source.Read("visual/game.json", 16384, metrics));
        impl_->backdrop = LoadPicture(renderer, source, impl_->definition.backdrop, metrics);
        impl_->logo = LoadPicture(renderer, source, impl_->definition.title_logo, metrics);
        impl_->atlas = LoadPicture(renderer, source, impl_->definition.atlas, metrics);
        impl_->detail = LoadPicture(renderer, source, impl_->definition.detail_atlas, metrics);
        impl_->environment = LoadPicture(renderer, source, impl_->definition.environment_atlas, metrics);
    }

    GamePresentation::~GamePresentation() = default;

    const VisualManifest& GamePresentation::definition() const
    {
        return impl_->definition;
    }

    rengine::Material GamePresentation::atlas_material() const
    {
        return impl_->atlas->material;
    }

    bool GamePresentation::HasLogo() const
    {
        return impl_->logo != nullptr;
    }

    rengine::Material GamePresentation::detail_material() const
    {
        return impl_->detail ? impl_->detail->material : rengine::Material{};
    }

    rengine::Draw GamePresentation::Backdrop(bool gameplay, std::uint32_t scene)
    {
        if (gameplay && scene != 0 && impl_->environment) {
            if (scene > impl_->definition.environments.size()) {
                throw std::runtime_error("presentation scene is missing from environment atlas");
            }
            auto& picture = *impl_->environment;
            picture.instance = rengine::Sprite(0, 0, 16, 9);
            const auto& uv = impl_->definition.environments[scene - 1];
            std::copy(uv.begin(), uv.end(), picture.instance.uv_rect);
            for (auto channel = 0u; channel < 3; ++channel) {
                picture.instance.color[channel] = .24f;
            }
            return picture.Draw();
        }
        if (impl_->backdrop == nullptr) {
            return {};
        }
        auto& picture = *impl_->backdrop;
        picture.instance = rengine::Sprite(0, 0, 16, 9);
        const auto brightness = gameplay ? impl_->definition.combat_brightness : 1.f;
        for (auto channel = 0u; channel < 3; ++channel) {
            picture.instance.color[channel] = brightness;
        }
        return picture.Draw();
    }

    rengine::Draw GamePresentation::Logo(float x, float y, float width)
    {
        if (impl_->logo == nullptr) {
            return {};
        }
        auto& picture = *impl_->logo;
        // フォントで作った文字画像を縦横に引き延ばさず、指定した幅に合わせる。
        picture.instance = rengine::Sprite(x, y, width, width * float(picture.height) / float(picture.width));
        return picture.Draw();
    }
} // namespace gengine
