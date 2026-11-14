#include "mcp.hpp"
#include <algorithm>
#include <array>
#include <set>

namespace gengine::editor
{
    namespace
    {
        Json RpcError(const Json& id, int code, const std::string& message)
        {
            return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
        }

        bool ValidRequest(const Json& value)
        {
            if (value.is_object() == false || value.value("jsonrpc", Json{}) != "2.0" ||
                value.contains("method") == false || value["method"].is_string() == false) {
                return false;
            }

            if (value.contains("id") && value["id"].is_string() == false && value["id"].is_number_integer() == false) {
                return false;
            }
            return true;
        }

        // 操作の失敗はJSON-RPC自体の失敗と分け、ツールの構造化結果として返す。
        Json ToolError(const ActionError& error)
        {
            const auto value = Json{{"error", AsJson(Encode(error))}};
            return {{"content", Json::array({{{"type", "text"}, {"text", value.dump()}}})},
                    {"structuredContent", value},
                    {"isError", true}};
        }
    } // namespace

    Json McpAdapter::ToolList() const
    {
        // 共通の操作定義を使い、MCP用の再送IDと版指定だけを引数へ補う。
        auto tools = Json::array();
        for (const auto& action : core_.dispatcher().List()) {
            auto input = AsJson(action.input_schema);
            input["properties"]["_operation_id"] = {{"type", "string"},
                                                    {"description",
                                                     "Replay ID, required for changes; same ID and input returns the "
                                                     "original result."}};
            input["properties"]["_version"] = {
                {"type", "integer"}, {"const", action.version}, {"default", action.version}};
            if (action.requires_operation_id) {
                input["required"].push_back("_operation_id");
            }

            // MCPのstructuredContentはobjectなので配列結果だけitemsで包む。
            auto output = AsJson(action.output_schema);
            if (output.value("type", "") != "object") {
                output = {{"type", "object"},
                          {"properties", {{"items", output}}},
                          {"required", Json::array({"items"})},
                          {"additionalProperties", false}};
            }

            tools.push_back({{"name", action.action_id},
                             {"description", action.description},
                             {"inputSchema", input},
                             {"outputSchema", output},
                             {"annotations",
                              {{"readOnlyHint", action.effect == "read"},
                               {"destructiveHint", action.effect == "file-output"},
                               {"openWorldHint", false}}}});
        }
        return {{"tools", tools}};
    }

    Json McpAdapter::CallTool(const Json& parameters)
    {
        // 通信の形式と操作名を確認し、共通Dispatcherへ渡す要求を組み立てる。
        if (parameters.is_object() == false || parameters.contains("name") == false ||
            parameters["name"].is_string() == false) {
            throw std::invalid_argument("tools/call requires a tool name");
        }

        auto arguments = parameters.value("arguments", Json::object());
        if (arguments.is_object() == false) {
            return ToolError({"InvalidArgument", "arguments", "arguments must be an object"});
        }

        auto request = Request{};
        request.action_id = parameters["name"].get<std::string>();
        const auto definitions = core_.dispatcher().List();
        if (std::none_of(definitions.begin(), definitions.end(), [&](const ActionInfo& action) {
                return action.action_id == request.action_id;
            })) {
            throw std::invalid_argument("unknown tool name");
        }

        // 再送IDと版は通信側の共通項目なので、操作固有の引数から取り除く。
        try {
            request.operation_id = arguments.value("_operation_id", std::string{});
            if (arguments.contains("_version")) {
                const auto version = Decode<std::uint32_t>(FromJson(arguments["_version"]));
                if (version.has_value() == false) {
                    return ToolError(version.error());
                }
                request.version = *version;
            }

            arguments.erase("_operation_id");
            arguments.erase("_version");
            request.arguments = FromJson(arguments);
        } catch (const std::exception&) {
            return ToolError({"InvalidArgument", "arguments", "arguments or common metadata have an invalid type"});
        }

        // GUIと同じ所有スレッドで実行する。コンパイルなら受付結果までを返す。
        auto& dispatcher = core_.dispatcher();
        const auto accepted = dispatcher.Submit(request);
        if (accepted.has_value() == false) {
            return ToolError(accepted.error());
        }

        const auto processed = dispatcher.Pump();
        if (processed.has_value() == false) {
            return ToolError(processed.error());
        }
        const auto operation = dispatcher.Lookup(accepted->operation_id).operation;
        if (operation->error.has_value()) {
            return ToolError(*operation->error);
        }

        // 構造化結果を使わないクライアントにも、同じ内容をテキストで渡す。
        auto output = AsJson(operation->result);
        if (output.is_object() == false) {
            output = {{"items", output}};
        }
        auto content = Json::array({{{"type", "text"}, {"text", output.dump()}}});
        if (request.action_id == "viewport.capture") {
            const auto png = core_.TakeCapture();
            if (png.empty()) {
                return ToolError({"CaptureExpired", {}, "image payload is not retained; request a new capture"});
            }
            constexpr auto alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            auto encoded = std::string{};
            for (std::size_t i = 0; i < png.size(); i += 3) {
                const auto value = (std::uint32_t(png[i]) << 16) |
                                   (i + 1 < png.size() ? std::uint32_t(png[i + 1]) << 8 : 0) |
                                   (i + 2 < png.size() ? png[i + 2] : 0);
                encoded += alphabet[(value >> 18) & 63];
                encoded += alphabet[(value >> 12) & 63];
                encoded += i + 1 < png.size() ? alphabet[(value >> 6) & 63] : '=';
                encoded += i + 2 < png.size() ? alphabet[value & 63] : '=';
            }
            content.push_back({{"type", "image"}, {"mimeType", "image/png"}, {"data", encoded}});
        }
        return {{"content", content}, {"structuredContent", output}, {"isError", false}};
    }

    std::optional<Json> McpAdapter::Handle(const Json& message)
    {
        if (ValidRequest(message) == false) {
            return RpcError(nullptr, -32600, "Invalid JSON-RPC request");
        }

        // 通知には応答しない。初期化完了の通知を受けてからツール操作を許可する。
        const auto method = message["method"].get<std::string>();
        if (message.contains("id") == false) {
            if (method == "notifications/initialized" && initialized_) {
                ready_ = true;
            }
            return {};
        }

        const auto id = message["id"];
        try {
            const auto parameters = message.value("params", Json::object());
            if (parameters.is_object() == false) {
                return RpcError(id, -32602, "params must be an object");
            }

            // 接続時の初期化、疎通確認、操作一覧・実行をJSON-RPCのメソッドで振り分ける。
            auto result = Json::object();
            if (method == "initialize") {
                if (initialized_ || parameters.contains("protocolVersion") == false ||
                    parameters["protocolVersion"].is_string() == false ||
                    parameters.contains("capabilities") == false || parameters["capabilities"].is_object() == false ||
                    parameters.contains("clientInfo") == false || parameters["clientInfo"].is_object() == false) {
                    return RpcError(id, -32602, "Invalid or repeated initialize");
                }

                initialized_ = true;
                result = {{"protocolVersion", "2025-11-25"},
                          {"capabilities", {{"tools", Json::object()}}},
                          {"serverInfo", {{"name", "gEngine Editor"}, {"version", GENGINE_VERSION}}},
                          {"instructions", "Paths are relative to the workspace passed at startup. Read "
                                           "editor.get_state before changing a project. compile.start returns "
                                           "Accepted; poll job.get until terminal. Use job.cancel for accepted "
                                           "jobs."}};
            }
            else if (method == "ping") {
                result = Json::object();
            }
            else if (ready_ == false) {
                return RpcError(id, -32000, "Complete initialize and notifications/initialized first");
            }
            else if (method == "tools/list") {
                if (parameters.contains("cursor")) {
                    return RpcError(id, -32602, "This fixed tool catalog has no continuation cursor");
                }
                result = ToolList();
            }
            else if (method == "tools/call") {
                result = CallTool(parameters);
            }
            else {
                return RpcError(id, -32601, "Method not found");
            }

            return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
        } catch (const std::exception& error) {
            return RpcError(id, -32602, std::string(error.what()).substr(0, 1024));
        }
    }

    std::vector<std::string> McpAdapter::Receive(const std::vector<std::string>& messages)
    {
        auto parsed = std::vector<Json>{};
        auto responses = std::vector<std::string>{};
        auto cancelled = std::set<std::string>{};

        // 同じ受信キュー内の取消し通知を先に集め、実行前の要求へ適用できるようにする。
        for (const auto& text : messages) {
            try {
                auto message = ParseJson(text);
                if (ValidRequest(message) && message["method"] == "notifications/cancelled" &&
                    message.contains("id") == false) {
                    const auto parameters = message.value("params", Json::object());
                    if (parameters.is_object() && parameters.contains("requestId")) {
                        cancelled.insert(parameters["requestId"].dump());
                    }
                }
                parsed.push_back(std::move(message));
            } catch (const std::exception&) {
                responses.push_back(RpcError(nullptr, -32700, "Invalid JSON or message limit exceeded").dump());
            }
        }

        for (const auto& message : parsed) {
            // 未開始の要求だけを取り消す。受付済みジョブの取消しはjob.cancelの責務。
            if (ValidRequest(message) && message.contains("id") && message["method"] != "initialize" &&
                cancelled.contains(message["id"].dump())) {
                continue;
            }

            if (const auto response = Handle(message); response.has_value()) {
                responses.push_back(response->dump());
            }
        }
        return responses;
    }

    StdioTransport::StdioTransport()
    {
        // stdio接続ではコンソール入力を待たず、クライアントが渡したパイプだけを使う。
        if (GetFileType(GetStdHandle(STD_INPUT_HANDLE)) != FILE_TYPE_PIPE ||
            GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) != FILE_TYPE_PIPE) {
            throw std::invalid_argument("--mcp requires stdin and stdout pipes from an MCP client");
        }

        // 読み書きは別々に待機する。送信側の起動に失敗した場合も受信側を残さない。
        reader_ = std::thread([this] {
            ReadLoop();
        });
        try {
            writer_ = std::thread([this] {
                WriteLoop();
            });
        } catch (...) {
            Stop();
            throw;
        }
    }

    StdioTransport::~StdioTransport()
    {
        Stop();
    }

    void StdioTransport::ReadLoop()
    {
        auto pending = std::string{};
        auto buffer = std::array<char, 4096>{};

        // パイプから届く断片を、改行で区切られた一件のメッセージへ組み立てる。
        while (stopping_.load() == false) {
            DWORD bytes = 0;
            if (::ReadFile(GetStdHandle(STD_INPUT_HANDLE), buffer.data(), static_cast<DWORD>(buffer.size()), &bytes,
                           nullptr) == FALSE ||
                bytes == 0) {
                break;
            }

            for (DWORD index = 0; index < bytes; ++index) {
                if (buffer[index] == '\n') {
                    // 受信スレッドでは解析・実行せず、容量制限付きのキューへ渡す。
                    const auto lock = std::lock_guard(mutex_);
                    if (incoming_.size() >= 128 || incoming_bytes_ + pending.size() > 2 * 1024 * 1024) {
                        disconnected_ = true;
                        return;
                    }
                    incoming_bytes_ += pending.size();
                    incoming_.push_back(std::move(pending));
                    pending.clear();
                }
                else {
                    pending += buffer[index];
                    if (pending.size() > 1024 * 1024) {
                        disconnected_ = true;
                        return;
                    }
                }
            }
        }

        // EOFや読込み失敗は所有スレッドへ知らせ、Editor全体の終了につなげる。
        disconnected_ = true;
    }

    void StdioTransport::WriteLoop()
    {
        while (stopping_.load() == false) {
            auto message = std::string{};

            // 送信する一件だけを取り出し、書込み待ちの間はキューのロックを解放する。
            {
                auto lock = std::unique_lock(mutex_);
                changed_.wait(lock, [this] {
                    return stopping_.load() || outgoing_.empty() == false;
                });
                if (stopping_.load()) {
                    return;
                }
                message = std::move(outgoing_.front());
                outgoing_.pop_front();
                outgoing_bytes_ -= message.size();
            }

            // 一度で全量を書けない場合も続けて送る。切断時は再送せず終了を通知する。
            std::size_t offset = 0;
            while (offset < message.size() && stopping_.load() == false) {
                DWORD written = 0;
                if (WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message.data() + offset,
                              static_cast<DWORD>(message.size() - offset), &written, nullptr) == FALSE ||
                    written == 0) {
                    disconnected_ = true;
                    return;
                }
                offset += written;
            }
        }
    }

    std::vector<std::string> StdioTransport::Take()
    {
        // 所有スレッドへ受信済みの要求をまとめて渡し、キューの使用量を戻す。
        const auto lock = std::lock_guard(mutex_);
        auto result = std::vector<std::string>{};
        while (incoming_.empty() == false) {
            result.push_back(std::move(incoming_.front()));
            incoming_.pop_front();
        }
        incoming_bytes_ = 0;
        return result;
    }

    void StdioTransport::Send(std::string message)
    {
        // 所有スレッドをパイプ待ちで止めず、改行付きの応答を送信キューへ積む。
        message += '\n';
        const auto lock = std::lock_guard(mutex_);
        if (outgoing_.size() >= 128 || outgoing_bytes_ + message.size() > 2 * 1024 * 1024) {
            disconnected_ = true;
            return;
        }

        outgoing_bytes_ += message.size();
        outgoing_.push_back(std::move(message));
        changed_.notify_one();
    }

    void StdioTransport::Stop()
    {
        stopping_ = true;
        changed_.notify_all();

        // 他プロセスがパイプを開いたままでもGUI終了を妨げない。
        // 停止要求の直後にI/O待ちへ入る競合もあるため、スレッド終了まで取消しを繰り返す。
        if (reader_.joinable()) {
            while (WaitForSingleObject(reader_.native_handle(), 0) == WAIT_TIMEOUT) {
                CancelSynchronousIo(reader_.native_handle());
                WaitForSingleObject(reader_.native_handle(), 10);
            }
            reader_.join();
        }

        if (writer_.joinable()) {
            while (WaitForSingleObject(writer_.native_handle(), 0) == WAIT_TIMEOUT) {
                CancelSynchronousIo(writer_.native_handle());
                WaitForSingleObject(writer_.native_handle(), 10);
            }
            writer_.join();
        }
    }
} // namespace gengine::editor
