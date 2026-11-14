#include "editor_core.hpp"
#include "mcp.hpp"
#include <fstream>
#include <iostream>

using namespace gengine::editor;

namespace
{
    void Require(bool condition, const char* message)
    {
        if (condition == false) {
            throw std::runtime_error(message);
        }
    }

    // GUIと同じ型付き操作の入口を使い、受付後の成功・失敗を検査できるようにする。
    template <class Input>
    Operation Action(EditorCore& editor, std::string id, const Input& input, std::string operation_id = {})
    {
        const auto accepted = editor.dispatcher().SubmitTyped(std::move(id), input, std::move(operation_id));
        Require(accepted.has_value(), "request was not accepted");
        Require(editor.dispatcher().Pump().has_value(), "pump failed");
        return *editor.dispatcher().Lookup(accepted->operation_id).operation;
    }

    template <class Output, class Input>
    Output Success(EditorCore& editor, std::string id, const Input& input, std::string operation_id = {})
    {
        const auto result = Action(editor, id, input, std::move(operation_id));
        if (result.error.has_value()) {
            throw std::runtime_error(id + ": " + result.error->reason);
        }
        return Decode<Output>(result.result).value();
    }

    void Overwrite(const std::filesystem::path& path, std::string_view bytes)
    {
        auto output = std::ofstream(path, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        Require(output.good(), "fixture write failed");
    }

    JobInfo Wait(EditorCore& editor, const std::string& id)
    {
        // 画面のタイマーに代わってジョブを進め、試験自体にも待機時間の上限を設ける。
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (std::chrono::steady_clock::now() < limit) {
            editor.Tick();
            auto job = Success<JobInfo>(editor, "job.get", JobRequest{id});
            if (job.state != "Queued" && job.state != "Running") {
                return job;
            }
            Sleep(5);
        }
        throw std::runtime_error("job timeout");
    }

    void AutomaticWorkspace(const std::filesystem::path& root, const std::filesystem::path& compiler)
    {
        // 実行ファイルの場所から、外にある日本語・空白を含むプロジェクトを選ぶ状態を再現する。
        const auto initial = root / L"launch";
        const auto selected = root / L"日本語 sample";
        const auto other = root / L"other sample";
        for (const auto& directory : {initial, selected, other}) {
            std::filesystem::create_directory(directory);
        }
        const auto settings = R"({"format_version":1,"project_id":"sample","sources":["game.glang"],"scenes":[]})";
        for (const auto& directory : {selected, other}) {
            Overwrite(directory / L"project.json", settings);
            Overwrite(directory / L"game.glang", "export fn main() -> i32 { return 1; }\n");
        }

        // 固定workspaceでは絶対パスを許可せず、通常GUIだけが選択先へ切り替わる。
        const auto path = ProjectPathRequest{PathText(selected / L"project.json")};
        auto fixed = EditorCore(initial, compiler);
        Require(Action(fixed, "project.open", path, NewId()).error->code == "InvalidArgument",
                "fixed workspace accepted absolute path");
        auto editor = EditorCore(initial, compiler, true);
        const auto project = *Success<EditorState>(editor, "project.open", path, NewId()).project;
        Require(editor.workspace() == selected && project.path == "project.json", "selected project root");
        const auto other_path = ProjectPathRequest{PathText(other / L"project.json")};
        Require(Action(editor, "project.open", other_path, NewId()).error->code == "InvalidState" &&
                    editor.workspace() == selected,
                "open project allowed workspace replacement");

        // ロック中の選択先へ移れない場合は、選択前の作業場所を維持する。
        auto contender = EditorCore(initial, compiler, true);
        Require(Action(contender, "project.open", path, NewId()).error->code == "Busy" &&
                    contender.workspace() == initial,
                "failed lock changed workspace");

        // 入力も成果物も選択先を基準にし、閉じた後は通常の相対パスでも開き直せる。
        const auto job =
            Wait(editor,
                 Success<CompileAccepted>(editor, "compile.start",
                                          CompileRequest{project.project_id, project.revision, "game.glang"}, NewId())
                     .job_id);
        Require(job.state == "Succeeded" && job.artifact.has_value(), "selected project compilation");
        Require(std::filesystem::is_regular_file(selected / Wide(*job.artifact)) &&
                    std::filesystem::exists(initial / L".gengine") == false,
                "artifact escaped selected project");
        Success<EditorState>(editor, "project.close", CloseRequest{project.project_id, project.revision}, NewId());

        // 不正JSONとプロジェクト外の参照を拒否し、失敗後も前の作業場所から再開できる。
        Overwrite(other / L"project.json", "{");
        Require(Action(editor, "project.open", other_path, NewId()).error->code == "InvalidArgument" &&
                    editor.workspace() == selected,
                "invalid JSON changed workspace");
        auto invalid = ParseJson(settings);
        invalid["sources"] = Json::array({"../日本語 sample/game.glang"});
        Overwrite(other / L"project.json", invalid.dump());
        Require(Action(editor, "project.open", other_path, NewId()).error->code == "InvalidArgument" &&
                    editor.workspace() == selected,
                "automatic workspace accepted escaping source");
        Require(Success<EditorState>(editor, "editor.get_state", EmptyRequest{}).project.has_value() == false,
                "failed selection opened a project");
        const auto reopened =
            *Success<EditorState>(editor, "project.open", ProjectPathRequest{"project.json"}, NewId()).project;
        Success<EditorState>(editor, "project.close", CloseRequest{reopened.project_id, reopened.revision}, NewId());

        // 同名・同IDの別プロジェクトでも、前の成果物を読み込ませない。
        Overwrite(other / L"project.json", settings);
        const auto switched = *Success<EditorState>(editor, "project.open", other_path, NewId()).project;
        Require(editor.workspace() == other && switched.revision > reopened.revision, "second project root");
        Require(Action(editor, "vm.load", VmLoadRequest{job.job_id}, NewId()).error->code == "InvalidState",
                "previous workspace artifact accepted");
    }

    void ProjectsAndCompiler(const std::filesystem::path& workspace, const std::filesystem::path& compiler)
    {
        // 不正な作成先を拒否した後も、日本語・空白を含む通常のパスを使えることを確認する。
        auto editor = EditorCore(workspace, compiler);
        Require(Success<EditorState>(editor, "editor.get_state", EmptyRequest{}).project.has_value() == false,
                "initial state");
        for (const auto* path :
             {"../escape", "C:/escape", "/rooted", "bad:stream", "NUL", "folder/../escape", "bad.", ".gengine"}) {
            Require(Action(editor, "project.create", ProjectPathRequest{path}, NewId()).state == "Failed",
                    "path boundary");
        }

        // 同じ要求の再送は作成し直さず、別の読込みで現在のプロジェクトを破棄しない。
        const auto created =
            Success<EditorState>(editor, "project.create", ProjectPathRequest{"日本語 space"}, "create");
        const auto project = *created.project;
        const auto replay =
            Success<EditorState>(editor, "project.create", ProjectPathRequest{"日本語 space"}, "create");
        Require(replay.project->revision == project.revision, "create replay duplicated project");
        Require(Action(editor, "project.open", ProjectPathRequest{project.path}, "open-active").error->code ==
                    "InvalidState",
                "open must not discard current project");

        const auto path = workspace / Wide(project.path);
        const auto source = path.parent_path() / L"scripts/game.glang";
        const auto settings = ReadFile(path, 64 * 1024);

        // 別Editorの同時書込みを拒否する。失敗しても最初のEditorは使える。
        auto other = EditorCore(workspace, compiler);
        Require(Action(other, "project.open", ProjectPathRequest{project.path}, "locked").error->code == "Busy",
                "project lock");

        // 古い版への要求は拒否し、現在の版なら一件だけ非同期ジョブを受け付ける。
        Require(Action(editor, "compile.start",
                       CompileRequest{project.project_id, project.revision + 1, "scripts/game.glang"}, "stale")
                        .error->code == "Conflict",
                "revision conflict");

        const auto request = CompileRequest{project.project_id, project.revision, "scripts/game.glang"};
        const auto accepted = Success<CompileAccepted>(editor, "compile.start", request, "compile1");
        Require(accepted.status == "Accepted", "compile is asynchronous");
        Require(Success<JobInfo>(editor, "job.get", JobRequest{accepted.job_id}).state == "Queued",
                "queued before tick");
        Require(Success<CompileAccepted>(editor, "compile.start", request, "compile1").job_id == accepted.job_id,
                "compile replay");
        Require(Action(editor, "project.close", CloseRequest{project.project_id, project.revision}, "close-busy")
                        .error->code == "Busy",
                "close during compile");
        Require(Action(editor, "compile.start", request, "compile-busy").error->code == "Busy", "concurrent compile");

        // 受付後に元ソースを変えても、固定した入力だけをコンパイルする。
        Overwrite(source, "export fn main() -> i32 { return true; }\n");
        auto completed = Wait(editor, accepted.job_id);
        Require(completed.state == "Succeeded" && completed.artifact.has_value(), "real compiler success");
        const auto artifact_path = workspace / Wide(*completed.artifact);
        const auto previous = ReadFile(artifact_path, 1024 * 1024);
        Require(Sha256(previous) == *completed.artifact_hash, "artifact identity");
        Require(completed.source_hash != Sha256(ReadFile(source, 1024 * 1024)), "source snapshot identity");

        // 型エラーは元の位置へ対応づけ、前回成功した成果物を上書きしない。
        const auto broken = Success<CompileAccepted>(editor, "compile.start", request, "broken");
        completed = Wait(editor, broken.job_id);
        Require(completed.state == "Failed" && completed.artifact.has_value() == false &&
                    completed.diagnostics.size() == 1,
                "compile error state");
        Require(completed.diagnostics[0].line == 1 && completed.diagnostics[0].column.has_value() &&
                    completed.diagnostics[0].source == "scripts/game.glang",
                "mapped source diagnostic");
        Require(ReadFile(artifact_path, 1024 * 1024) == previous, "failure overwrote previous artifact");

        // 取消し要求と完了を区別し、取消し後も修正済みソースを再コンパイルできる。
        Overwrite(source, "export fn main() -> i32 { return 7; }\n");
        const auto cancelled = Success<CompileAccepted>(editor, "compile.start", request, "cancelled");
        const auto cancelling = Success<JobInfo>(editor, "job.cancel", JobRequest{cancelled.job_id}, "cancel");
        Require(cancelling.state == "Queued" && cancelling.cancel_requested, "cancel request is not completion");
        Require(Wait(editor, cancelled.job_id).state == "Cancelled", "queued cancellation");
        Require(ReadFile(artifact_path, 1024 * 1024) == previous, "cancel overwrote previous artifact");

        const auto retry = Success<CompileAccepted>(editor, "compile.start", request, "retry");
        Require(Wait(editor, retry.job_id).state == "Succeeded", "corrected retry");

        // 開いた後の設定変更は検出し、開閉を繰り返しても版と排他ロックを更新する。
        Overwrite(path, settings + " ");
        Require(Action(editor, "compile.start", request, "changed-settings").error->code == "Conflict",
                "externally changed settings");
        Overwrite(path, settings);
        Success<EditorState>(editor, "project.close", CloseRequest{project.project_id, project.revision}, "close");
        for (int index = 0; index < 20; ++index) {
            const auto opened = Success<EditorState>(editor, "project.open", ProjectPathRequest{project.path}, NewId());
            Require(opened.project->revision > project.revision, "revision must not repeat after reopen");
            Success<EditorState>(editor, "project.close",
                                 CloseRequest{opened.project->project_id, opened.project->revision}, NewId());
        }

        // 設定内の参照先も検査し、失敗した読込みでは現在の状態を変更しない。
        auto invalid = ParseJson(settings);
        for (const auto* reference : {"../outside.glang", "C:/outside.glang", ".gengine/x.glang"}) {
            invalid["sources"] = Json::array({reference});
            Overwrite(path, invalid.dump());
            Require(Action(editor, "project.open", ProjectPathRequest{project.path}, NewId()).state == "Failed",
                    "source path boundary");
            Require(Success<EditorState>(editor, "editor.get_state", EmptyRequest{}).project.has_value() == false,
                    "invalid open changed state");
        }

        Overwrite(path, settings);
        const auto reopened = Success<EditorState>(editor, "project.open", ProjectPathRequest{project.path}, "reopen");
        Require(Action(editor, "compile.start", request, "old-revision").error->code == "Conflict",
                "old session revision");
        const auto input =
            CompileRequest{reopened.project->project_id, reopened.project->revision, "scripts/game.glang"};

        // 履歴の上限を越えても、新しいジョブを受け付け、古い結果の欠落を明示する。
        for (int index = 0; index < 65; ++index) {
            const auto next = Success<CompileAccepted>(editor, "compile.start", input, NewId());
            Success<JobInfo>(editor, "job.cancel", JobRequest{next.job_id}, NewId());
            Require(Wait(editor, next.job_id).state == "Cancelled", "bounded job history cancellation");
        }
        Require(Action(editor, "job.get", JobRequest{accepted.job_id}).error->code == "NotFound",
                "old job retained beyond limit");

        // 終了後に一時ファイルと排他ロックが残らず、別のEditorで開けることを確認する。
        editor.Shutdown();
        Require(std::filesystem::is_empty(path.parent_path() / L".gengine/jobs"), "job intermediates remain");
        Require(Success<EditorState>(other, "project.open", ProjectPathRequest{project.path}, "unlocked")
                    .project.has_value(),
                "shutdown retained lock");
    }

    void InvalidInputs(const std::filesystem::path& workspace)
    {
        // コンパイラーが存在しなくてもジョブの失敗として扱い、プロジェクトを閉じられる。
        auto editor = EditorCore(workspace, workspace / L"missing-compiler.exe");
        Require(Action(editor, "project.create", ProjectPathRequest{".GENGINE"}, "reserved").state == "Failed",
                "reserved path is case insensitive");
        const auto project =
            *Success<EditorState>(editor, "project.create", ProjectPathRequest{"invalid-inputs"}, "create").project;
        const auto accepted = Success<CompileAccepted>(
            editor, "compile.start", CompileRequest{project.project_id, project.revision, "scripts/game.glang"},
            "missing-compiler");
        const auto failed = Wait(editor, accepted.job_id);
        Require(failed.state == "Failed" && failed.artifact.has_value() == false && failed.diagnostics.empty() == false,
                "missing compiler failure");
        Success<EditorState>(editor, "project.close", CloseRequest{project.project_id, project.revision}, "close");

        // 壊れたJSON、同じファイルの別名、未対応の形式版をそれぞれ拒否する。
        const auto path = workspace / Wide(project.path);
        const auto document = ParseJson(ReadFile(path, 64 * 1024));
        Overwrite(path, "{");
        Require(Action(editor, "project.open", ProjectPathRequest{project.path}, "invalid-json").error->code ==
                    "InvalidArgument",
                "invalid project JSON");

        auto duplicate = document;
        duplicate["sources"] = Json::array({"scripts/game.glang", "SCRIPTS/game.glang"});
        Overwrite(path, duplicate.dump());
        Require(Action(editor, "project.open", ProjectPathRequest{project.path}, "duplicate").state == "Failed",
                "case-alias source duplication");

        auto unknown = document;
        unknown["format_version"] = 2;
        Overwrite(path, unknown.dump());
        Require(Action(editor, "project.open", ProjectPathRequest{project.path}, "future-format").state == "Failed",
                "future format");
        Overwrite(path, document.dump(2));
    }

    void RunningCancellation(const std::filesystem::path& workspace, const std::filesystem::path& slow_compiler)
    {
        // 終了の遅い試験用の子を起動し、実行中の取消しを確実に通す。
        auto editor = EditorCore(workspace, slow_compiler);
        const auto project =
            *Success<EditorState>(editor, "project.create", ProjectPathRequest{"cancel-running"}, "create").project;
        const auto request = CompileRequest{project.project_id, project.revision, "scripts/game.glang"};
        auto accepted = Success<CompileAccepted>(editor, "compile.start", request, "start");
        editor.Tick();
        Require(Success<JobInfo>(editor, "job.get", JobRequest{accepted.job_id}).state == "Running",
                "child did not start");
        Success<JobInfo>(editor, "job.cancel", JobRequest{accepted.job_id}, "cancel");
        Require(Wait(editor, accepted.job_id).state == "Cancelled", "running process cancellation");

        // 再起動した子も、Editor終了時に残さず回収する。
        accepted = Success<CompileAccepted>(editor, "compile.start", request, "start-again");
        editor.Tick();
        const auto start = std::chrono::steady_clock::now();
        editor.Shutdown();
        Require(std::chrono::steady_clock::now() - start < std::chrono::seconds(3), "shutdown left child running");
    }

    Json Rpc(McpAdapter& adapter, const Json& request)
    {
        const auto responses = adapter.Receive({request.dump()});
        Require(responses.size() == 1, "one response required");
        return ParseJson(responses.front());
    }

    void Protocol(const std::filesystem::path& workspace, const std::filesystem::path& compiler)
    {
        auto core = EditorCore(workspace, compiler);
        auto adapter = McpAdapter(core);

        // JSONとJSON-RPCの異常を区別し、初期化前のツール呼出しを拒否する。
        Require(ParseJson(adapter.Receive({"{"})[0])["error"]["code"] == -32700, "malformed JSON");
        Require(ParseJson(adapter.Receive({"[]"})[0])["error"]["code"] == -32600, "batch not allowed");
        Require(ParseJson(adapter.Receive({"{\"jsonrpc\":\"2.0\",\"id\":1,\"id\":2}"})[0])["error"]["code"] == -32700,
                "duplicate key");
        Require(Rpc(adapter, {{"jsonrpc", "2.0"}, {"id", -5}, {"method", "tools/list"}}).contains("error"),
                "initialization gate");

        // 初期化後の一覧は共通Registryと一致し、結果スキーマはMCPのobject形式になる。
        const auto initialized = Rpc(adapter, {{"jsonrpc", "2.0"},
                                               {"id", -4},
                                               {"method", "initialize"},
                                               {"params",
                                                {{"protocolVersion", "2025-11-25"},
                                                 {"capabilities", Json::object()},
                                                 {"clientInfo", {{"name", "test"}, {"version", "1"}}}}}});
        Require(initialized["result"]["protocolVersion"] == "2025-11-25", "negotiated version");
        Require(adapter.Receive({R"({"jsonrpc":"2.0","method":"notifications/initialized"})"}).empty(),
                "notification response");
        const auto tools =
            Rpc(adapter, {{"jsonrpc", "2.0"}, {"id", "catalog"}, {"method", "tools/list"}})["result"]["tools"];
        Require(tools.size() == core.dispatcher().List().size(), "catalog diverged");
        for (const auto& tool : tools) {
            Require(tool["outputSchema"]["type"] == "object", "MCP structured output must be object");
        }

        // MCPと型付き操作の結果を比較し、未開始要求の取消しも確認する。
        const auto query = Json{{"jsonrpc", "2.0"},
                                {"id", 8},
                                {"method", "tools/call"},
                                {"params", {{"name", "editor.get_state"}, {"arguments", Json::object()}}}};
        const auto remote = Rpc(adapter, query)["result"]["structuredContent"];
        Require(remote == AsJson(Encode(Success<EditorState>(core, "editor.get_state", EmptyRequest{}))),
                "typed and MCP query mismatch");
        Require(adapter
                    .Receive({query.dump(),
                              R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":8}})"})
                    .empty(),
                "pending request cancellation");

        // 引数の型違いはツールの失敗、不明メソッドはJSON-RPCの失敗として返す。
        auto invalid = query;
        invalid["params"]["arguments"]["unknown"] = -1;
        Require(Rpc(adapter, invalid)["result"]["isError"] == true, "unsupported argument type");
        Require(Rpc(adapter, {{"jsonrpc", "2.0"}, {"id", 9}, {"method", "unknown"}})["error"]["code"] == -32601,
                "unknown method");
    }
    void Collections(const std::filesystem::path& workspace, const std::filesystem::path& compiler)
    {
        auto editor = EditorCore(workspace, compiler);
        const auto project =
            *Success<EditorState>(editor, "project.create", ProjectPathRequest{"collections"}, "create").project;
        const auto test_source =
            workspace / std::filesystem::path(Wide(project.path)).parent_path() / Wide(project.sources[0]);
        Overwrite(test_source, ReadFile(test_source, 1024 * 1024) + "\nexport fn exhaust() { while true {} }\n");
        const auto compile_request = CompileRequest{project.project_id, project.revision, project.sources[0]};
        const auto job =
            Wait(editor, Success<CompileAccepted>(editor, "compile.start", compile_request, "compile").job_id);
        Require(job.state == "Succeeded", "collection template compilation");
        const auto loaded = Success<EditorState>(editor, "vm.load", JobRequest{job.job_id}, "load");
        const auto vm_id = *loaded.vm_id;
        const auto vm = VmRequest{vm_id};
        // 所有量上限の失敗は現在のVMを残す。新しい設定だけが成功時に採用される。
        const auto original_metrics = Success<VmMetricsResult>(editor, "vm.get_metrics", vm);
        for (const auto limit : {std::uint64_t{0}, gvm_default_limits().max_owned_bytes + 1}) {
            Require(Action(editor, "vm.load", VmLoadRequest{job.job_id, limit}, NewId()).error->code ==
                        "InvalidArgument",
                    "invalid load limit");
        }
        const auto too_small = Action(editor, "vm.load", VmLoadRequest{job.job_id, 1}, "memory-limit");
        Require(too_small.error->code == "VmError" && too_small.error->reason.find("MemoryLimit") != std::string::npos,
                "memory failure reason");
        Require(Success<EditorState>(editor, "editor.get_state", EmptyRequest{}).vm_id == vm_id &&
                    Success<VmMetricsResult>(editor, "vm.get_metrics", vm).limits.max_owned_bytes ==
                        original_metrics.limits.max_owned_bytes,
                "failed load changed VM or limits");
        auto request = PoolRequest{
            vm_id, "particles2d", {{"position", 0}, {"position", 1}, {"life", 0}, {"serial", 0}, {"active", 0}}, 0, 16};
        const auto inspect = [&] {
            return Success<PoolSnapshot>(editor, "vm.inspect_pool", request);
        };
        const auto call = [&](std::string name, std::string id = {}) {
            return Success<ResetResult>(editor, "vm.call", VmCallRequest{vm_id, std::move(name)},
                                        id.empty() ? NewId() : id);
        };

        // 宣言時は空。型の異なる列とvec2/vec3の成分をホストから同じ入口で読む。
        Require(inspect().length == 0 && inspect().capacity == 128, "initial empty pool");
        call("initialize");
        const auto before = inspect();
        Require(before.length == 3 && before.count == 3 && before.columns[1].values[0] == "-2" &&
                    before.columns[2].type == "i32" && before.columns[3].type == "u32" &&
                    before.columns[4].type == "bool" && before.columns[4].values[0] == "true",
                "typed columns");
        call("update", "update-once");
        call("update", "update-once");
        Require(inspect().columns[0].values[0] == "1" && before.columns[0].values[0] == "0",
                "owned snapshot and deduplicated update");
        const auto three = Success<PoolSnapshot>(editor, "vm.inspect_pool",
                                                 PoolRequest{vm_id, "particles3d", {{"position", 2}}, 0, 16});
        Require(three.columns[0].values[0] == "-1", "vec3 z update");

        // 二列目で失敗しても借用は残らない。直後の実行・resetが成功することが証拠になる。
        auto invalid = request;
        invalid.columns.push_back({"missing", 0});
        Require(Action(editor, "vm.inspect_pool", invalid).state == "Failed", "missing column");
        invalid = request;
        invalid.columns = {{"position", 2}};
        Require(Action(editor, "vm.inspect_pool", invalid).state == "Failed", "vec2 component boundary");
        invalid = request;
        invalid.pool = "missing";
        Require(Action(editor, "vm.inspect_pool", invalid).state == "Failed", "missing pool");
        call("update");
        call("update");
        call("spawn");
        call("remove_expired");
        const auto compacted = inspect();
        Require(compacted.length == 1 && compacted.columns[3].values[0] == "3", "deletion compacts surviving rows");
        Require(Action(editor, "vm.call", VmCallRequest{vm_id, "main"}, NewId()).state == "Failed", "wrong signature");
        Require(Success<VmStateResult>(editor, "vm.get_state", vm).state == "Ready",
                "signature failure preserves state");

        // 同じ状態を型付き操作とMCPから読むと、列の文字列も件数も一致する。
        auto adapter = McpAdapter(editor);
        Rpc(adapter, {{"jsonrpc", "2.0"},
                      {"id", 1},
                      {"method", "initialize"},
                      {"params",
                       {{"protocolVersion", "2025-11-25"},
                        {"capabilities", Json::object()},
                        {"clientInfo", {{"name", "collections"}, {"version", "1"}}}}}});
        adapter.Receive({R"({"jsonrpc":"2.0","method":"notifications/initialized"})"});
        const auto remote =
            Rpc(adapter, {{"jsonrpc", "2.0"},
                          {"id", 2},
                          {"method", "tools/call"},
                          {"params", {{"name", "vm.inspect_pool"}, {"arguments", AsJson(Encode(request))}}}});
        Require(remote["result"]["structuredContent"] == AsJson(Encode(inspect())), "MCP snapshot parity");
        const auto measured = Success<VmMetricsResult>(editor, "vm.get_metrics", vm);
        const auto remote_metrics =
            Rpc(adapter, {{"jsonrpc", "2.0"},
                          {"id", 3},
                          {"method", "tools/call"},
                          {"params", {{"name", "vm.get_metrics"}, {"arguments", AsJson(Encode(vm))}}}});
        Require(remote_metrics["result"]["structuredContent"] == AsJson(Encode(measured)), "MCP metrics parity");
        const auto previous_call = Encode(measured.last_call);
        call("update", "update-once");
        Require(Encode(Success<VmMetricsResult>(editor, "vm.get_metrics", vm).last_call) == previous_call,
                "replayed operation must not execute or measure again");

        // Faultedを表示し、resetで空のReadyへ戻る。古いコピーは引き続き読める。
        Require(Action(editor, "vm.call", VmCallRequest{vm_id, "fail"}, "fault").state == "Failed", "runtime fault");
        Require(Success<VmStateResult>(editor, "vm.get_state", vm).state == "Faulted", "Faulted state");
        const auto diagnostic = Success<VmDiagnosticResult>(editor, "vm.get_diagnostics", vm);
        Require(diagnostic.present && diagnostic.line.has_value() && diagnostic.function == "fail",
                "source diagnostic");
        Require(Action(editor, "vm.call", VmCallRequest{vm_id, "update"}, NewId()).error->code == "InvalidState",
                "faulted call gate");
        Require(Action(editor, "vm.inspect_pool", request).error->code == "InvalidState", "faulted inspection gate");
        Success<ResetResult>(editor, "vm.reset", vm, "reset");
        Require(inspect().length == 0 && before.length == 3 &&
                    Success<VmDiagnosticResult>(editor, "vm.get_diagnostics", vm).present == false,
                "reset and snapshot independence");

        // 画面を占有する無限実行も固定fuelで止まり、resetで通常の操作へ戻れる。
        Require(Action(editor, "vm.call", VmCallRequest{vm_id, "exhaust"}, "exhaust").state == "Failed",
                "fuel stops loop");
        Require(Success<VmStateResult>(editor, "vm.get_state", vm).state == "Faulted", "fuel fault state");
        Success<ResetResult>(editor, "vm.reset", vm, "reset-fuel");

        // 容量を超える生成、表示上限、末尾以降の照会を実際の集合で確認する。
        for (auto index = 0; index < 130; ++index) {
            call("spawn");
        }
        request.limit = 64;
        Require(inspect().length == 128 && inspect().count == 64, "bounded snapshot and full pool");
        request.offset = UINT32_MAX;
        Require(inspect().count == 0 && inspect().offset == 128, "offset clamp without overflow");
        request.limit = 65;
        Require(Action(editor, "vm.inspect_pool", request).error->code == "InvalidArgument", "row cap");
        request.limit = 16;
        request.columns.resize(9, {"life", 0});
        Require(Action(editor, "vm.inspect_pool", request).error->code == "InvalidArgument", "column cap");

        // 読込み前に改変を検出し、失敗時には現在のインスタンスを保つ。
        const auto artifact_path = workspace / Wide(*job.artifact);
        const auto artifact = ReadFile(artifact_path, 64 * 1024 * 1024);
        Overwrite(artifact_path, "corrupted");
        Require(Action(editor, "vm.load", JobRequest{job.job_id}, "tampered").error->code == "Conflict",
                "artifact hash");
        Require(Success<EditorState>(editor, "editor.get_state", EmptyRequest{}).vm_id == vm_id,
                "failed load preserves VM");
        Overwrite(artifact_path, artifact);
        const auto source_path =
            workspace / std::filesystem::path(Wide(project.path)).parent_path() / Wide(project.sources[0]);
        const auto source = ReadFile(source_path, 1024 * 1024);
        Overwrite(source_path, source + "\n// edited\n");
        Require(Action(editor, "vm.load", JobRequest{job.job_id}, "edited").error->code == "Conflict", "source hash");
        Overwrite(source_path, source);

        // 入替え・解放・プロジェクト終了後に古いIDを再利用できない。
        const auto replacement =
            Success<EditorState>(editor, "vm.load", VmLoadRequest{job.job_id, 32 * 1024 * 1024}, "reload");
        Require(replacement.vm_id != vm_id, "instance ID renewal");
        const auto replaced_metrics = Success<VmMetricsResult>(editor, "vm.get_metrics", VmRequest{*replacement.vm_id});
        Require(replaced_metrics.limits.max_owned_bytes == 32 * 1024 * 1024 &&
                    replaced_metrics.last_call.has_value() == false &&
                    replaced_metrics.last_inspection.has_value() == false,
                "replacement limits or stale metrics");
        Require(Action(editor, "vm.reset", vm, "old-id").error->code == "NotFound", "stale instance");
        Success<EditorState>(editor, "vm.unload", VmRequest{*replacement.vm_id}, "unload");
        const auto final_vm = *Success<EditorState>(editor, "vm.load", JobRequest{job.job_id}, "load-final").vm_id;
        Success<EditorState>(editor, "project.close", CloseRequest{project.project_id, project.revision}, "close");
        Require(Action(editor, "vm.get_state", VmRequest{final_vm}).error->code == "NotFound", "close destroys VM");
        Success<EditorState>(editor, "project.open", ProjectPathRequest{project.path}, "reopen");
        Require(Action(editor, "vm.load", JobRequest{job.job_id}, "old-revision").state == "Failed",
                "old project revision");
    }
} // namespace

int wmain(int count, wchar_t** arguments)
{
    try {
        Require(count == 4, "usage: editor_test WORKSPACE COMPILER SLOW_COMPILER");

        // 実行ごとの作業領域を用意し、過去の試験入力や通常のプロジェクトを変更しない。
        const auto root = std::filesystem::path(arguments[1]) / Wide(NewId());
        std::filesystem::create_directories(root);
        AutomaticWorkspace(root, arguments[2]);
        ProjectsAndCompiler(root, arguments[2]);
        InvalidInputs(root);
        RunningCancellation(root, arguments[3]);
        Protocol(root, arguments[2]);
        Collections(root, arguments[2]);
        std::cout << "Editor project, compiler, cancellation and MCP tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
