"""実GameRunnerの通常起動・入力・上限付きJSON計測を検証する。"""
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import subprocess
import sys
import time
import uuid


def run(executable, output, game):
    output.mkdir(parents=True, exist_ok=True)
    report_path = output / (game + "-" + uuid.uuid4().hex + ".json")
    process = subprocess.Popen([str(executable), "--game", game, "--metrics", str(report_path),
                                "--warmup", "30", "--samples", "300", "--managed", "1"],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    user = ctypes.windll.user32
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
    window = None
    try:
        deadline = time.monotonic() + 15
        while window is None:
            assert time.monotonic() < deadline and process.poll() is None, "Runner startup failed"
            handles = []

            @callback_type
            def visit(hwnd, _):
                pid = wintypes.DWORD()
                user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                name = ctypes.create_unicode_buffer(128)
                user.GetClassNameW(hwnd, name, len(name))
                if pid.value == process.pid and name.value == "gEnginePlayView":
                    handles.append(hwnd)
                return True

            user.EnumWindows(visit, 0)
            window = handles[0] if handles else None
            time.sleep(0.02)
        # 描画領域を固定し、開発用プリセットを使わずEnterと通常キーで開始する。
        rect = wintypes.RECT(0, 0, 960, 540)
        user.AdjustWindowRect(ctypes.byref(rect), 0x10CF0000 | 0x02000000, False)
        user.SetWindowPos(window, None, 0, 0, rect.right - rect.left, rect.bottom - rect.top, 0x0002 | 0x0004)
        def key(code, down):
            assert user.PostMessageW(window, 0x100 if down else 0x101, code, 0)

        # ウィンドウ作成の後にも描画・音声初期化が続くため、入力はメッセージキューへ送る。
        key(13, True)
        key(13, False)
        key(32, True)
        began = time.monotonic()
        deadline = began + 20
        while not report_path.exists():
            assert time.monotonic() < deadline and process.poll() is None, "capture timed out"
            right = int((time.monotonic() - began) / 0.55) % 2 == 0
            key(ord("D"), right)
            key(ord("A"), not right)
            time.sleep(0.02)
        # 作成から書込み完了までの短い区間は、プロセス終了後に読む。
        user.PostMessageW(window, 0x10, 0, 0)
        stdout, stderr = process.communicate(timeout=5)
        assert process.returncode == 0, (stdout, stderr)
        report = json.loads(report_path.read_text(encoding="utf-8"))
        assert report["state"] == "Complete" and report["collected"] == 300, report
        assert report["source"] == "GameRunner" and report["game"] == game
        counters = {row["name"]: row["value"] for row in report["counters"]}
        assert counters["playing_frames"] > 0 and counters["faulted_frames"] == 0
        assert counters["width"] == 960 and counters["height"] == 540, counters
        assert report["timings"][7]["samples"] > 100
        (output / (game + "-latest.json")).write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(json.dumps({"game": game, "report": str(report_path), "collected": report["collected"]}))
    finally:
        if process.poll() is None:
            if window:
                user.PostMessageW(window, 0x10, 0, 0)
            try:
                process.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate()


if __name__ == "__main__":
    run(Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3])
