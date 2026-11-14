"""実EditorのPIEを通常のウィンドウ入力とMCPの両方から検証する。"""
import base64
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import sys
import time
import uuid
from editor_mcp_test import Client


def run(executable, workspace, source, game="2d"):
    workspace.mkdir(parents=True, exist_ok=True)
    (workspace / "game.glang").write_bytes(source.read_bytes())
    (workspace / "project.json").write_text(json.dumps({"format_version": 1, "project_id": "play-test", "sources": ["game.glang"], "scenes": []}), encoding="utf-8")
    client = Client(executable, workspace)
    user = ctypes.windll.user32
    user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user.EnumChildWindows.argtypes = [wintypes.HWND, callback_type, wintypes.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]

    def mutate(name, arguments):
        return client.tool(name, arguments, uuid.uuid4().hex)

    def find_view():
        handles = []

        @callback_type
        def child(hwnd, _):
            name = ctypes.create_unicode_buffer(128)
            user.GetClassNameW(hwnd, name, len(name))
            if name.value == "gEnginePlayView":
                handles.append(hwnd)
            return True

        @callback_type
        def top(hwnd, _):
            pid = wintypes.DWORD()
            user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if pid.value == client.process.pid:
                user.EnumChildWindows(hwnd, child, 0)
            return True

        user.EnumWindows(top, 0)
        assert len(handles) == 1, handles
        return handles[0]

    def key(view, value, down):
        # 実Editorのメッセージループを通し、ダイアログ操作による入力の横取りも検出する。
        assert user.PostMessageW(view, 0x100 if down else 0x101, value, 0), "cannot queue game input"

    try:
        client.initialize()
        opened = mutate("project.open", {"path": "project.json"})["project"]
        job = mutate("compile.start", {"project_id": opened["project_id"], "expected_revision": opened["revision"], "source": "game.glang"})
        compiled = client.wait(job["job_id"])
        assert compiled["state"] == "Succeeded"
        client.tool("play.start", {"job_id": job["job_id"], "preset": 0, "game": "4d"}, uuid.uuid4().hex, expect_error=True)
        client.tool("play.start", {"job_id": job["job_id"], "preset": 0, "game": "2d" if game == "3d" else "3d"}, uuid.uuid4().hex, expect_error=True)
        op = uuid.uuid4().hex
        started = client.tool("play.start", {"job_id": job["job_id"], "preset": 0, "game": game}, op)
        assert client.tool("play.start", {"job_id": job["job_id"], "preset": 0, "game": game}, op) == started
        assert started["game"] == game
        play = {"play_id": started["play_id"]}
        view = find_view()
        client.tool("compile.start", {"project_id": opened["project_id"], "expected_revision": opened["revision"],
                                      "source": "game.glang"}, uuid.uuid4().hex, expect_error=True)
        client.tool("project.close", {"project_id": opened["project_id"], "expected_revision": opened["revision"]},
                    uuid.uuid4().hex, expect_error=True)

        def capture(name):
            result = client.request("tools/call", {"name": "viewport.capture", "arguments": play})
            assert result.get("isError") is False, result
            png = base64.b64decode(result["content"][1]["data"])
            assert png[:8] == b"\x89PNG\r\n\x1a\n"
            (workspace / name).write_bytes(png)
            return result["structuredContent"]

        assert capture("start.png")["phase"] == "Start"
        # 開始画面で計測操作を検証し、通信待ちで通常プレイの入力開始を遅らせない。
        client.tool("performance.get", {"play_id": "stale"}, expect_error=True)
        client.tool("performance.start", {**play, "config": {"warmup": 0, "samples": 3601}}, uuid.uuid4().hex, expect_error=True)
        capture_id = uuid.uuid4().hex
        configuration = {**play, "config": {"warmup": 2, "samples": 12}}
        accepted_capture = client.tool("performance.start", configuration, capture_id)
        assert client.tool("performance.start", configuration, capture_id) == accepted_capture
        client.tool("performance.start", configuration, uuid.uuid4().hex, expect_error=True)
        time.sleep(0.5)
        measured = client.tool("performance.get", play)
        assert measured["state"] == "Complete" and measured["collected"] == 12, measured
        assert measured["source"] == "EditorPIE" and measured["game"] == game
        assert measured["process_end"]["private_bytes"] > 0
        assert client.tool("performance.get", play) == measured
        client.tool("performance.start", {**play, "config": {"warmup": 600, "samples": 300}}, uuid.uuid4().hex)
        stopped_capture = client.tool("performance.stop", play, uuid.uuid4().hex)
        assert stopped_capture["state"] == "Stopped" and stopped_capture["collected"] == 0

        key(view, 13, True)
        key(view, 13, False)
        assert client.tool("play.get_state", play)["phase"] == "Playing", "Editor consumed Enter before PIE"

        # PもMCP操作で代用せず、文字入力を含む通常経路から一時停止・再開する。
        key(view, ord("P"), True)
        key(view, ord("P"), False)
        paused = client.tool("play.get_state", play)
        assert paused["paused"] is True, "Editor consumed pause input before PIE"
        time.sleep(0.05)
        assert client.tool("play.get_state", play)["tick"] == paused["tick"]
        key(view, ord("P"), True)
        key(view, ord("P"), False)
        assert client.tool("play.get_state", play)["paused"] is False

        key(view, 32, True)
        deadline = time.monotonic() + 20
        captured = False
        while time.monotonic() < deadline:
            state = client.tool("play.get_state", play)
            if state["phase"] != "Playing":
                break
            # フォーカス喪失による正規のPauseを観測し、通常キーで再開して射撃も入れ直す。
            if state["paused"]:
                key(view, ord("P"), True)
                key(view, ord("P"), False)
                key(view, 32, True)
            period = (state["tick"] + 18) % 72
            key(view, ord("D"), period < 36)
            key(view, ord("A"), period >= 36)
            if state["tick"] > 60 and not captured:
                capture("playing.png")
                captured = True
            time.sleep(0.01)
        assert state["phase"] == "Result", state
        assert state["outcome"] == 1, state
        capture("result.png")
        key(view, 27, True)
        key(view, 27, False)
        assert capture("returned.png")["phase"] == "Start"
        assert (workspace / "start.png").read_bytes() == (workspace / "returned.png").read_bytes(), "initial and reset title renders differ"
        key(view, 13, True)
        key(view, 13, False)
        paused = mutate("play.pause", dict(play, paused=True))
        time.sleep(0.1)
        assert client.tool("play.get_state", play)["tick"] == paused["tick"]
        mutate("play.pause", dict(play, paused=False))
        deadline = time.monotonic() + 6
        while time.monotonic() < deadline:
            state = client.tool("play.get_state", play)
            if state["phase"] == "Result":
                break
            if state["paused"]:
                key(view, ord("P"), True)
                key(view, ord("P"), False)
            time.sleep(0.02)
        assert state["outcome"] == 2, state
        capture("failure.png")
        key(view, 13, True)
        key(view, 13, False)
        assert client.tool("play.get_state", play)["phase"] == "Playing"
        mutate("play.reset", dict(play, normal=True))
        assert client.tool("play.get_state", play)["phase"] == "Start"
        mutate("play.stop", play)
        client.tool("play.get_state", play, expect_error=True)
        for preset in range(1, 5):
            trial = mutate("play.start", {"job_id": job["job_id"], "preset": preset, "game": game})
            assert trial["phase"] == "Playing" and trial["preset"] == preset
            trial_id = {"play_id": trial["play_id"]}
            reset = mutate("play.reset", trial_id)
            assert reset["tick"] == 0 and reset["preset"] == preset
            normal = mutate("play.reset", dict(trial_id, normal=True))
            assert normal["phase"] == "Start" and normal["preset"] == 0
            mutate("play.stop", trial_id)
        artifact = workspace / compiled["artifact"]
        original = artifact.read_bytes()
        artifact.write_bytes(b"corrupted")
        client.tool("play.start", {"job_id": job["job_id"], "preset": 0, "game": game}, uuid.uuid4().hex, expect_error=True)
        artifact.write_bytes(original)
        (workspace / "game.glang").write_bytes(source.read_bytes() + b"\n// changed after compilation\n")
        client.tool("play.start", {"job_id": job["job_id"], "preset": 0, "game": game}, uuid.uuid4().hex, expect_error=True)
        # 動作中のPIEを残してEOFを送り、終了時の資源回収も通す。
        (workspace / "game.glang").write_bytes(source.read_bytes())
        mutate("play.start", {"job_id": job["job_id"], "preset": 0, "game": game})
        print("PIE normal input: clear, title, replay, pause, reset, capture and stale ID passed")
    finally:
        client.process.stdin.close()
        assert client.process.wait(timeout=10) == 0
        client.log.close()


if __name__ == "__main__":
    run(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), Path(sys.argv[3]).resolve(), sys.argv[4] if len(sys.argv) > 4 else "2d")
