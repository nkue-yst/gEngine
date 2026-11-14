"""実Editorのstdio経路を検証する。--sdkでは公式MCPクライアントも使う。"""

import argparse
import asyncio
import hashlib
import json
from pathlib import Path
import queue
import subprocess
import threading
import time
import uuid


class Client:
    def __init__(self, executable, workspace):
        # 実Editorの標準入出力を使い、診断ログだけを別ファイルへ分ける。
        self.log = (workspace / "server.log").open("wb")
        self.process = subprocess.Popen(
            [str(executable), "--mcp", "--workspace", str(workspace)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log,
        )

        # 読込み待ちを別スレッドへ分け、応答が来ない場合は試験側で期限を判定する。
        self.messages = queue.Queue()
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()
        self.sequence = 0

    def _read(self):
        for line in self.process.stdout:
            self.messages.put(line)

    def send(self, message):
        self.process.stdin.write((json.dumps(message, ensure_ascii=False) + "\n").encode("utf-8"))
        self.process.stdin.flush()

    def receive(self):
        return json.loads(self.messages.get(timeout=10))

    def request(self, method, params=None):
        # 応答IDを照合し、プロトコル上の失敗をツール操作の失敗と区別する。
        self.sequence += 1
        message = {"jsonrpc": "2.0", "id": self.sequence, "method": method}
        if params is not None:
            message["params"] = params
        self.send(message)
        response = self.receive()
        assert response["id"] == self.sequence, response
        assert "error" not in response, response
        return response["result"]

    def initialize(self):
        initialized = self.request("initialize", {
            "protocolVersion": "2025-11-25", "capabilities": {},
            "clientInfo": {"name": "gEngine protocol acceptance", "version": "1"},
        })
        assert initialized["protocolVersion"] == "2025-11-25"
        self.send({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def tool(self, name, arguments=None, operation_id=None, expect_error=False):
        args = dict(arguments or {})
        if operation_id is not None:
            args["_operation_id"] = operation_id
        result = self.request("tools/call", {"name": name, "arguments": args})
        assert result.get("isError", False) == expect_error, result

        # テキストを使うクライアントと構造化結果を使うクライアントで内容が一致する。
        assert json.loads(result["content"][0]["text"]) == result["structuredContent"]
        return result["structuredContent"]

    def wait(self, job):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            result = self.tool("job.get", {"job_id": job})
            if result["state"] in ("Succeeded", "Failed", "Cancelled"):
                return result
            time.sleep(0.01)
        raise AssertionError("compile did not finish")

    def close(self):
        # EOFによる通常終了を待つ。試験失敗時にも子プロセスとパイプを残さない。
        if self.process.stdin.closed is False:
            self.process.stdin.close()
        try:
            assert self.process.wait(timeout=5) == 0
        finally:
            if self.process.poll() is None:
                self.process.kill()
                self.process.wait()
            self.reader.join(timeout=2)
            self.process.stdout.close()
            self.log.close()


def transport_test(executable, workspace):
    client = Client(executable, workspace)
    try:
        client.initialize()
        tools = client.request("tools/list")["tools"]
        assert len(tools) == 50

        # 日本語を含むパスで作成し、同じ操作IDの再送と内容違いを区別する。
        state = client.tool("project.create", {"path": "日本語 space"}, "create")
        project = state["project"]
        assert client.tool("project.create", {"path": "日本語 space"}, "create") == state
        conflicting = client.tool("project.create", {"path": "different"}, "create", expect_error=True)
        assert conflicting["error"]["code"] == "Conflict"

        # コンパイル受付から完了まで照会し、成果物の内容と入力ソース名を確認する。
        request = {"project_id": project["project_id"], "expected_revision": project["revision"], "source": project["sources"][0]}
        accepted = client.tool("compile.start", request, "compile")
        assert accepted["status"] == "Accepted"
        result = client.wait(accepted["job_id"])
        assert result["state"] == "Succeeded", result
        artifact = workspace / result["artifact"]
        previous = artifact.read_bytes()
        assert hashlib.sha256(previous).hexdigest() == result["artifact_hash"]
        assert b"scripts/game.glang" in previous, "bytecode lost the stable source name"

        # 実ウィンドウの所有スレッドでVMを操作し、借用が通信応答に残らないことを確認する。
        loaded = client.tool("vm.load", {"job_id": result["job_id"]}, "load")
        vm = {"vm_id": loaded["vm_id"]}
        client.tool("vm.call", {**vm, "function": "initialize"}, "initialize")
        inspection = {**vm, "pool": "particles3d", "columns": [{"field": "position", "component": 2}], "offset": 0, "limit": 16}
        snapshot = client.tool("vm.inspect_pool", inspection)
        assert snapshot["length"] == 3 and snapshot["columns"][0]["values"][0] == "-3"
        client.tool("vm.call", {**vm, "function": "update"}, "update")
        assert client.tool("vm.inspect_pool", inspection)["columns"][0]["values"][0] == "-1"
        report = client.tool("vm.get_metrics", vm)
        assert sum(report["memory"].values()) == report["vm"]["stats"]["owned_bytes"]
        assert report["last_call"]["succeeded"] is True
        assert report["last_inspection"]["source_value_bytes"] == 12
        assert client.tool("vm.call", {**vm, "function": "update", "fuel": 0}, "zero", expect_error=True)["error"]["code"] == "InvalidArgument"
        client.tool("vm.call", {**vm, "function": "update", "fuel": 1}, "fuel-fault", expect_error=True)
        assert client.tool("vm.get_diagnostics", vm)["code"] == "FuelExhausted"
        failed_metrics = client.tool("vm.get_metrics", vm)
        assert failed_metrics["last_call"]["fuel_budget"] == 1 and failed_metrics["last_call"]["succeeded"] is False
        client.tool("vm.reset", vm, "fuel-reset")
        assert client.tool("vm.get_metrics", vm)["last_call"] is None
        client.tool("vm.call", {**vm, "function": "initialize_batch", "fuel": 1000000}, "batch")
        assert client.tool("vm.inspect_pool", inspection)["length"] == 128
        denied = client.tool("vm.load", {"job_id": result["job_id"], "max_owned_bytes": 1}, "too-small", expect_error=True)
        assert "MemoryLimit" in denied["error"]["reason"]
        assert client.tool("editor.get_state")["vm_id"] == vm["vm_id"]
        client.tool("vm.call", {**vm, "function": "fail"}, "fail", expect_error=True)
        assert client.tool("vm.get_state", vm)["state"] == "Faulted"
        assert client.tool("vm.get_diagnostics", vm)["present"] is True
        client.tool("vm.reset", vm, "reset")
        assert client.tool("vm.inspect_pool", inspection)["length"] == 0
        assert client.tool("vm.unload", vm, "unload")["vm_id"] is None

        # 型エラーでは診断を返して前回の成果物を保持し、修正後は再び成功する。
        source = workspace / "日本語 space/scripts/game.glang"
        source.write_text("export fn main() -> i32 { return true; }\n", encoding="utf-8")
        failed = client.wait(client.tool("compile.start", request, "broken")["job_id"])
        assert failed["state"] == "Failed" and failed["artifact"] is None
        assert failed["diagnostics"][0]["line"] == 1 and failed["diagnostics"][0]["source"] == request["source"]
        assert artifact.read_bytes() == previous

        source.write_text("export fn main() -> i32 { return 8; }\n", encoding="utf-8")
        assert client.wait(client.tool("compile.start", request, "fixed")["job_id"])["state"] == "Succeeded"

        # 元の操作結果を照会でき、開き直したプロジェクトでは版が進む。
        lookup = client.tool("operation.get", {"operation_id": "compile"})
        assert lookup["operation"]["result"] == accepted
        close_request = {key: request[key] for key in ("project_id", "expected_revision")}
        assert client.tool("project.close", close_request, "close")["project"] is None
        opened = client.tool("project.open", {"path": project["path"]}, "open")["project"]
        assert opened["revision"] > project["revision"]

        # EOF時の子の停止・後始末も、実Editorの経路で確認する。
        request["expected_revision"] = opened["revision"]
        client.tool("compile.start", request, "disconnect")
    finally:
        client.close()
    assert not list((workspace / "日本語 space/.gengine/jobs").iterdir())

    # 不完全なJSONと要求の型違いにはエラー応答し、その後も初期化・疎通確認できる。
    malformed = Client(executable, workspace)
    try:
        malformed.process.stdin.write(b"{\n[]\n")
        malformed.process.stdin.flush()
        assert malformed.receive()["error"]["code"] == -32700
        assert malformed.receive()["error"]["code"] == -32600
        malformed.initialize()
        assert malformed.request("ping") == {}
    finally:
        malformed.close()

    # 容量を超えるメッセージは蓄積し続けず、Editor側から接続を終了する。
    oversized = Client(executable, workspace)
    try:
        try:
            oversized.process.stdin.write(b"x" * (1024 * 1024 + 1))
            oversized.process.stdin.flush()
        except BrokenPipeError:
            pass
        assert oversized.process.wait(timeout=5) == 0
    finally:
        try:
            oversized.close()
        except BrokenPipeError:
            oversized.reader.join(timeout=2)
            oversized.log.close()
    print("stdio acceptance passed: success, error, retry, replay, EOF, malformed and oversized input")


async def sdk_test(executable, workspace):
    from mcp import ClientSession, StdioServerParameters
    from mcp.client.stdio import stdio_client
    import jsonschema

    # 自前のクライアントだけでなく、公式SDKから初期化とツール取得を通す。
    parameters = StdioServerParameters(command=str(executable), args=["--mcp", "--workspace", str(workspace)])
    with (workspace / "sdk-server.log").open("w", encoding="utf-8") as log:
        async with stdio_client(parameters, errlog=log) as streams:
            async with ClientSession(*streams) as session:
                initialized = await session.initialize()
                assert initialized.protocolVersion == "2025-11-25"
                tools = await session.list_tools()
                schemas = {tool.name: tool for tool in tools.tools}
                assert len(schemas) == 50

                async def call(name, arguments=None):
                    # 実際に公開されたスキーマで、送る引数と返った結果の両方を検証する。
                    args = arguments or {}
                    jsonschema.validate(args, schemas[name].inputSchema)
                    result = await session.call_tool(name, args)
                    assert result.isError is False, result
                    jsonschema.validate(result.structuredContent, schemas[name].outputSchema)
                    return result.structuredContent

                # 作成・コンパイル・結果照会・終了までを、SDKの通常の呼出し方で実行する。
                project = (await call("project.create", {"path": "official-sdk", "_operation_id": "create"}))["project"]
                assert (await call("editor.get_state"))["project"] == project
                assert len((await call("action.list"))["items"]) == 50
                accepted = await call("compile.start", {
                    "project_id": project["project_id"], "expected_revision": project["revision"],
                    "source": project["sources"][0], "_operation_id": "compile",
                })
                for _ in range(200):
                    job = await call("job.get", {"job_id": accepted["job_id"]})
                    if job["state"] in ("Succeeded", "Failed", "Cancelled"):
                        break
                    await asyncio.sleep(0.01)
                assert job["state"] == "Succeeded", job
                loaded = await call("vm.load", {"job_id": job["job_id"], "max_owned_bytes": 33554432, "_operation_id": "load"})
                vm = {"vm_id": loaded["vm_id"]}
                await call("vm.call", {**vm, "function": "initialize", "fuel": 1000000, "_operation_id": "init"})
                snapshot = await call("vm.inspect_pool", {**vm, "pool": "particles2d", "columns": [{"field": "position", "component": 1}], "offset": 0, "limit": 16})
                assert snapshot["count"] == 3 and snapshot["columns"][0]["values"][0] == "-2"
                await call("vm.get_state", vm)
                report = await call("vm.get_metrics", vm)
                assert sum(report["memory"].values()) == report["vm"]["stats"]["owned_bytes"]
                assert report["limits"]["max_owned_bytes"] == 33554432
                await call("vm.get_diagnostics", vm)
                await call("vm.reset", {**vm, "_operation_id": "reset"})
                await call("vm.unload", {**vm, "_operation_id": "unload"})
                await call("project.close", {"project_id": project["project_id"], "expected_revision": project["revision"], "_operation_id": "close"})
                # 試遊と画像も公式SDKの通常のツール呼出しで確認する。
                for dimension in ("2d", "3d"):
                    game_source = Path(__file__).resolve().parents[1] / f"samples/shooter{dimension}/game.glang"
                    (workspace / "sdk-game.glang").write_bytes(game_source.read_bytes())
                    (workspace / "sdk-game.json").write_text(json.dumps({"format_version": 1, "project_id": "sdk-game", "sources": ["sdk-game.glang"], "scenes": []}), encoding="utf-8")
                    game_project = (await call("project.open", {"path": "sdk-game.json", "_operation_id": f"game-open-{dimension}"}))["project"]
                    # 新しい素材操作も公開スキーマを検証し、登録後のrevisionでコンパイルする。
                    asset_name = "sphere.gmesh" if dimension == "3d" else "sprite.png"
                    (workspace / asset_name).write_bytes((executable.parent / "content/assets" / asset_name).read_bytes())
                    game_project = (await call("asset.register", {
                        "project_id": game_project["project_id"], "expected_revision": game_project["revision"],
                        "path": asset_name, "_operation_id": f"asset-register-{dimension}"
                    }))["project"]
                    catalog = await call("asset.list")
                    assert len(catalog["entries"]) == 1 and not catalog["entries"][0]["diagnostic"]
                    accepted = await call("compile.start", {"project_id": game_project["project_id"], "expected_revision": game_project["revision"], "source": "sdk-game.glang", "_operation_id": f"game-compile-{dimension}"})
                    for _ in range(200):
                        job = await call("job.get", {"job_id": accepted["job_id"]})
                        if job["state"] in ("Succeeded", "Failed", "Cancelled"):
                            break
                        await asyncio.sleep(0.01)
                    assert job["state"] == "Succeeded", job
                    play = await call("play.start", {"job_id": job["job_id"], "preset": 0, **({"game": dimension} if dimension == "3d" else {}), "_operation_id": f"game-start-{dimension}"})
                    # 計測の入出力も公式SDKのスキーマで照合し、完了後だけGPU画像を取得する。
                    measurement = {"play_id": play["play_id"]}
                    await call("performance.start", {**measurement, "config": {"warmup": 2, "samples": 12},
                                                     "_operation_id": f"measure-start-{dimension}"})
                    for _ in range(200):
                        report = await call("performance.get", measurement)
                        if report["state"] == "Complete":
                            break
                        await asyncio.sleep(0.01)
                    assert report["state"] == "Complete" and report["collected"] == 12, report
                    await call("performance.stop", {**measurement, "_operation_id": f"measure-stop-{dimension}"})
                    capture = await session.call_tool("viewport.capture", {"play_id": play["play_id"]})
                    assert capture.isError is False and capture.content[1].type == "image", capture
                    jsonschema.validate(capture.structuredContent, schemas["viewport.capture"].outputSchema)
                    await call("play.stop", {"play_id": play["play_id"], "_operation_id": f"game-stop-{dimension}"})
                    assert play["game"] == dimension
                    await call("project.close", {"project_id": game_project["project_id"], "expected_revision": game_project["revision"], "_operation_id": f"game-close-{dimension}"})
    print("official MCP Python SDK 1.26.0 acceptance passed; all used input/output schemas validated")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("workspace", type=Path)
    parser.add_argument("--sdk", action="store_true")
    args = parser.parse_args()

    # 各回の入力を分け、SDKが未導入の環境でも標準ライブラリによる通信試験は実行できる。
    workspace = args.workspace.resolve() / str(uuid.uuid4())
    workspace.mkdir(parents=True)
    transport_test(args.executable.resolve(), workspace)
    if args.sdk:
        asyncio.run(sdk_test(args.executable.resolve(), workspace))


if __name__ == "__main__":
    main()
