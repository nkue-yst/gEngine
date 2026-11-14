#include "scene_document.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <windows.h>

namespace gengine
{
    namespace
    {
        using Json = nlohmann::json;
        void Require(bool valid, std::string_view field)
        {
            if (valid == false) {
                throw std::invalid_argument(std::string(field));
            }
        }
        void Fields(const Json& value, std::initializer_list<std::string_view> allowed)
        {
            Require(value.is_object(), "expected an object");
            Require(value.size() == allowed.size(), "missing or unknown scene field");
            for (const auto field : allowed) {
                Require(value.contains(field), std::string("missing field: ") + std::string(field));
            }
        }
        std::string Text(const Json& value)
        {
            Require(value.is_string(), "expected a string");
            return value.get<std::string>();
        }
        float Number(const Json& value)
        {
            Require(value.is_number(), "expected a number");
            const auto number = value.get<double>();
            Require(std::isfinite(number) && std::abs(number) <= std::numeric_limits<float>::max(),
                    "number must fit finite f32");
            return static_cast<float>(number);
        }
        std::int32_t Hp(const Json& value)
        {
            Require(value.is_number_integer(), "HP must be an integer");
            const auto number = value.get<double>();
            Require(number >= 1 && number <= std::numeric_limits<std::int32_t>::max(), "HP out of range");
            return value.get<std::int32_t>();
        }
        std::vector<float> Vector(const Json& value)
        {
            Require(value.is_array() && value.size() <= 3, "expected a vector with at most three components");
            auto result = std::vector<float>{};
            for (const auto& component : value) {
                result.push_back(Number(component));
            }
            return result;
        }
        std::optional<std::string> Reference(const Json& value)
        {
            return value.is_null() ? std::nullopt : std::optional(Text(value));
        }
        bool ValidId(std::string_view id)
        {
            return id.empty() == false && id.size() <= 128 && std::all_of(id.begin(), id.end(), [](unsigned char c) {
                       return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                              c == '_' || c == '.';
                   });
        }
        void CheckVector(const std::vector<float>& vector, std::size_t dimension)
        {
            Require(vector.size() == dimension && std::all_of(vector.begin(), vector.end(),
                                                              [](float v) {
                                                                  return std::isfinite(v);
                                                              }),
                    "invalid vector dimension or nonfinite component");
        }
        void Positive(float value)
        {
            Require(std::isfinite(value) && value > 0, "expected a positive finite value");
        }
        std::filesystem::path ReferencePath(std::string_view text)
        {
            Require(text.empty() == false && text.size() <= 1024 && text.find_first_of("\\:*?\"<>|") == text.npos,
                    "reference must be a project relative path");
            Require(std::none_of(text.begin(), text.end(),
                                 [](unsigned char c) {
                                     return c < 32;
                                 }),
                    "control character in reference");
            const auto path =
                std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
            Require(path.is_relative() && path.has_root_path() == false, "absolute reference is forbidden");
            for (const auto& part : path) {
                const auto encoded = part.u8string();
                auto name = std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
                Require(name.empty() == false && name != "." && name != ".." && name.back() != '.' &&
                            name.back() != ' ',
                        "invalid reference component");
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
                    return static_cast<char>(std::toupper(c));
                });
                const auto base = name.substr(0, name.find('.'));
                Require(name != ".GENGINE" && base != "CON" && base != "PRN" && base != "AUX" && base != "NUL" &&
                            (base.size() == 4 && (base.starts_with("COM") || base.starts_with("LPT")) &&
                             base[3] >= '0' && base[3] <= '9') == false,
                        "reserved reference component");
            }
            return path;
        }
        Json ParseDocumentJson(std::string_view text)
        {
            // 巨大な入力と重複キーを入口で拒否し、後の値による暗黙の上書きを防ぐ。
            Require(text.size() <= 1024 * 1024, "scene exceeds 1 MiB");
            auto keys = std::vector<std::set<std::string>>{};
            return Json::parse(text, [&](int depth, Json::parse_event_t event, Json& value) {
                Require(depth <= 16, "scene JSON nesting limit exceeded");
                if (event == Json::parse_event_t::object_start) {
                    keys.emplace_back();
                }
                else if (event == Json::parse_event_t::key) {
                    Require(keys.back().insert(value.get<std::string>()).second, "duplicate scene JSON key");
                }
                else if (event == Json::parse_event_t::object_end) {
                    keys.pop_back();
                }
                return true;
            });
        }
    } // namespace

    SceneDocument LoadProjectScene(const std::filesystem::path& project, std::string_view scene_path)
    {
        const auto read = [](const std::filesystem::path& path) {
            auto stream = std::ifstream(path, std::ios::binary | std::ios::ate);
            if (stream.is_open() == false || stream.tellg() < 0 || stream.tellg() > 1024 * 1024) {
                throw std::invalid_argument("cannot read scene or project file");
            }
            auto text = std::string(static_cast<std::size_t>(stream.tellg()), '\0');
            stream.seekg(0);
            if (stream.read(text.data(), static_cast<std::streamsize>(text.size())).good() == false) {
                throw std::invalid_argument("cannot read complete scene or project file");
            }
            return text;
        };
        const auto root = std::filesystem::absolute(project).parent_path();
        const auto config = ParseDocumentJson(read(project));
        if (config.contains("assets")) {
            Fields(config, {"format_version", "project_id", "sources", "scenes", "assets"});
            Require(config["assets"].is_null() || (config["assets"].is_array() && config["assets"].size() <= 256),
                    "invalid asset registration list");
        }
        else {
            Fields(config, {"format_version", "project_id", "sources", "scenes"});
        }
        Require(config["format_version"].is_number_integer() && config["format_version"] == 1 &&
                    config["scenes"].is_array(),
                "unsupported project format");
        const auto relative = ReferencePath(scene_path);
        Require(std::find(config["scenes"].begin(), config["scenes"].end(), std::string(scene_path)) !=
                    config["scenes"].end(),
                "scene is not registered in project");
        auto path = root;
        for (const auto& component : relative) {
            path /= component;
            const auto attributes = GetFileAttributesW(path.c_str());
            Require(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0,
                    "missing or linked scene path");
        }
        auto scene = ParseScene(read(path));
        ValidateSceneReferences(scene, root);
        return scene;
    }

    SceneDocument DefaultScene(bool three_dimensional)
    {
        auto scene = SceneDocument{};
        scene.game = three_dimensional ? "3d" : "2d";
        scene.document_id = "scene-" + scene.game;
        scene.settings.bounds_min =
            three_dimensional ? std::vector<float>{-8, 0.5f, -6} : std::vector<float>{-8, -4.5f};
        scene.settings.bounds_max = three_dimensional ? std::vector<float>{8, 6, 6} : std::vector<float>{8, 4.5f};
        scene.objects.push_back(DefaultSceneObject(scene.game, "player_spawn", "player"));
        scene.objects.push_back(DefaultSceneObject(scene.game, "enemy_spawn", "enemy"));
        scene.objects.push_back(DefaultSceneObject(scene.game, "game_camera", "camera"));
        return scene;
    }

    SceneObject DefaultSceneObject(std::string_view game, std::string_view kind, std::string id)
    {
        Require(game == "2d" || game == "3d", "unknown game dimension");
        auto object = SceneObject{};
        object.id = std::move(id);
        object.kind = kind;
        if (kind == "player_spawn") {
            object.position = game == "3d" ? std::vector<float>{0, 2, 4} : std::vector<float>{0, -3};
        }
        else if (kind == "enemy_spawn") {
            object.position = game == "3d" ? std::vector<float>{0, 2, -4} : std::vector<float>{0, 3};
        }
        else if (kind == "game_camera") {
            if (game == "3d") {
                object.position = {12, 10, 14};
                object.target = {0, 2, 0};
            }
            else {
                object.view_size = {16, 9};
            }
        }
        else {
            throw std::invalid_argument("unknown scene object kind");
        }
        return object;
    }

    void ValidateScene(const SceneDocument& scene)
    {
        // 文書全体の次元と移動条件を先に確定し、各対象を同じ座標系で検査する。
        Require(scene.format_version == 1 && ValidId(scene.document_id), "unsupported scene version or document ID");
        Require(scene.game == "2d" || scene.game == "3d", "unknown game dimension");
        const auto dimension = scene.game == "3d" ? 3u : 2u;
        const auto& settings = scene.settings;
        CheckVector(settings.bounds_min, dimension);
        CheckVector(settings.bounds_max, dimension);
        for (auto axis = 0u; axis < dimension; ++axis) {
            Require(settings.bounds_min[axis] < settings.bounds_max[axis], "bounds_min must be less than bounds_max");
        }
        Require(settings.player_hp > 0 && settings.enemy_hp > 0, "player_hp and enemy_hp must be positive");
        Require(std::isfinite(settings.move_speed) && settings.move_speed > 0,
                "settings.move_speed: expected a positive finite value");
        Require(std::isfinite(settings.fire_interval_seconds) && settings.fire_interval_seconds > 0,
                "settings.fire_interval_seconds: expected a positive finite value");
        Require(scene.objects.size() <= 3, "only one player, enemy and camera are supported");
        // IDと役割の重複を区別する。編集中は必須対象が欠けていても保持できる。
        auto ids = std::set<std::string>{};
        auto kinds = std::set<std::string>{};
        for (const auto& object : scene.objects) {
            Require(ValidId(object.id) && ids.insert(object.id).second, "invalid or duplicate object ID");
            Require(kinds.insert(object.kind).second, "duplicate object kind");
            if (object.kind == "player_spawn" || object.kind == "enemy_spawn") {
                CheckVector(object.position, dimension);
                for (auto axis = 0u; axis < dimension; ++axis) {
                    Require(object.position[axis] >= settings.bounds_min[axis] &&
                                object.position[axis] <= settings.bounds_max[axis],
                            "position is outside bounds_min / bounds_max");
                }
                Require(object.target.empty() && object.view_size.empty(), "camera fields on spawn object");
                Positive(object.fire_interval_seconds);
                for (const auto* ref : {&object.visual, &object.audio}) {
                    if (ref->has_value()) {
                        ReferencePath(**ref);
                    }
                }
            }
            else if (object.kind == "game_camera") {
                Require(object.visual.has_value() == false && object.audio.has_value() == false,
                        "camera cannot have asset references");
                if (dimension == 2) {
                    CheckVector(object.view_size, 2);
                    Positive(object.view_size[0]);
                    Positive(object.view_size[1]);
                    Require(object.position.empty() && object.target.empty(), "3D fields on 2D camera");
                }
                else {
                    CheckVector(object.position, 3);
                    CheckVector(object.target, 3);
                    Require(object.view_size.empty(), "2D fields on 3D camera");
                    Positive(object.fov_degrees);
                    Require(object.fov_degrees < 180, "fov_degrees must be less than 180");
                    // 上方向と視線が平行だとビュー行列を作れない。
                    const auto dx = double(object.target[0]) - object.position[0];
                    const auto dz = double(object.target[2]) - object.position[2];
                    Require(dx * dx + dz * dz > 1e-12,
                            "position / target: camera direction is parallel to up vector or zero");
                }
            }
            else {
                throw std::invalid_argument("unknown scene object kind");
            }
        }
    }

    SceneDocument ParseScene(std::string_view text)
    {
        const auto json = ParseDocumentJson(text);
        Fields(json, {"format_version", "document_id", "game", "settings", "objects"});
        Require(json["format_version"].is_number_integer() && json["format_version"] == 1,
                "unsupported scene format version");
        // 一時文書へ読み込み、全項目の検証を通った値だけを呼出し側へ返す。
        auto scene = SceneDocument{};
        scene.document_id = Text(json["document_id"]);
        scene.game = Text(json["game"]);
        const auto& settings = json["settings"];
        Fields(settings, {"bounds_min", "bounds_max", "player_hp", "enemy_hp", "move_speed", "fire_interval_seconds"});
        scene.settings = {Vector(settings["bounds_min"]), Vector(settings["bounds_max"]),
                          Hp(settings["player_hp"]),      Hp(settings["enemy_hp"]),
                          Number(settings["move_speed"]), Number(settings["fire_interval_seconds"])};
        Require(json["objects"].is_array() && json["objects"].size() <= 3, "invalid object list");
        for (const auto& value : json["objects"]) {
            auto object = SceneObject{};
            object.kind = Text(value.at("kind"));
            object.id = Text(value.at("id"));
            if (object.kind == "player_spawn" || object.kind == "enemy_spawn") {
                if (object.kind == "player_spawn") {
                    Fields(value, {"id", "kind", "position", "visual", "audio"});
                }
                else {
                    Fields(value, {"id", "kind", "position", "fire_interval_seconds", "visual", "audio"});
                    object.fire_interval_seconds = Number(value["fire_interval_seconds"]);
                }
                object.position = Vector(value["position"]);
                object.visual = Reference(value["visual"]);
                object.audio = Reference(value["audio"]);
            }
            else if (object.kind == "game_camera" && scene.game == "2d") {
                Fields(value, {"id", "kind", "view_size"});
                object.view_size = Vector(value["view_size"]);
            }
            else if (object.kind == "game_camera" && scene.game == "3d") {
                Fields(value, {"id", "kind", "position", "target", "fov_degrees"});
                object.position = Vector(value["position"]);
                object.target = Vector(value["target"]);
                object.fov_degrees = Number(value["fov_degrees"]);
            }
            else {
                throw std::invalid_argument("unsupported object kind or game dimension");
            }
            scene.objects.push_back(std::move(object));
        }
        ValidateScene(scene);
        return scene;
    }

    std::string SerializeScene(const SceneDocument& scene)
    {
        // 型付きデータも再検証する。ファイルへの書込みはこの関数では行わない。
        ValidateScene(scene);
        const auto& s = scene.settings;
        auto json = Json{{"format_version", scene.format_version},
                         {"document_id", scene.document_id},
                         {"game", scene.game},
                         {"settings",
                          {{"bounds_min", s.bounds_min},
                           {"bounds_max", s.bounds_max},
                           {"player_hp", s.player_hp},
                           {"enemy_hp", s.enemy_hp},
                           {"move_speed", s.move_speed},
                           {"fire_interval_seconds", s.fire_interval_seconds}}},
                         {"objects", Json::array()}};
        for (const auto& object : scene.objects) {
            auto value = Json{{"id", object.id}, {"kind", object.kind}};
            if (object.kind == "game_camera") {
                if (scene.game == "2d") {
                    value["view_size"] = object.view_size;
                }
                else {
                    value["position"] = object.position;
                    value["target"] = object.target;
                    value["fov_degrees"] = object.fov_degrees;
                }
            }
            else {
                value["position"] = object.position;
                value["visual"] = object.visual.has_value() ? Json(*object.visual) : Json(nullptr);
                value["audio"] = object.audio.has_value() ? Json(*object.audio) : Json(nullptr);
                if (object.kind == "enemy_spawn") {
                    value["fire_interval_seconds"] = object.fire_interval_seconds;
                }
            }
            json["objects"].push_back(std::move(value));
        }
        return json.dump(2) + "\n";
    }

    void ValidateSceneForPlay(const SceneDocument& scene)
    {
        ValidateScene(scene);
        Require(scene.objects.size() == 3, "play requires player_spawn, enemy_spawn and game_camera");
    }

    void ValidateSceneReferences(const SceneDocument& scene, const std::filesystem::path& project_root)
    {
        ValidateScene(scene);
        const auto root_attributes = GetFileAttributesW(project_root.c_str());
        Require(root_attributes != INVALID_FILE_ATTRIBUTES && (root_attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                    (root_attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0,
                "project root is missing or linked");
        for (const auto& object : scene.objects) {
            for (const auto* reference : {&object.visual, &object.audio}) {
                if (reference->has_value() == false) {
                    continue;
                }
                auto path = project_root;
                for (const auto& component : ReferencePath(**reference)) {
                    path /= component;
                    const auto attributes = GetFileAttributesW(path.c_str());
                    Require(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0,
                            "missing asset or linked reference");
                }
                Require(std::filesystem::is_regular_file(path), "asset must be a regular file");
            }
        }
    }
} // namespace gengine
