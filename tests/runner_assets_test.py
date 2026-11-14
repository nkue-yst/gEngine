"""通常ファイルを置かずにGameRunnerをpackだけで起動し、失敗診断と終了も確認する。"""
import ctypes
from ctypes import wintypes
from pathlib import Path
import shutil
import subprocess
import sys
import time


user = ctypes.windll.user32
callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user.IsWindowVisible.argtypes = [wintypes.HWND]


def windows(pid):
    result = []

    @callback
    def visit(hwnd, _):
        owner = wintypes.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid:
            title = ctypes.create_unicode_buffer(1024)
            user.GetWindowTextW(hwnd, title, len(title))
            result.append((hwnd, title.value))
        return True

    user.EnumWindows(visit, 0)
    return result


def run(executable, pack, output):
    deployed = output / '日本語 空白'
    deployed.mkdir(parents=True, exist_ok=True)
    target = deployed / executable.name
    shutil.copy2(executable, target)
    shutil.copytree(executable.parent / 'backends', deployed / 'backends', dirs_exist_ok=True)
    data = deployed / '素材.bpack'
    shutil.copy2(pack, data)
    assert not (deployed / 'content').exists(), 'pack-only deployment contains loose files'

    # 実体EXE・別cwd・日本語パスを通し、起動後の描画と入力を実ウィンドウで処理する。
    for game in ('2d', '3d'):
        process = subprocess.Popen([str(target), '--game', game, '--pack', str(data)], cwd=output)
        try:
            deadline = time.monotonic() + 15
            hwnd = None
            while time.monotonic() < deadline:
                assert process.poll() is None, 'runner exited during startup'
                found = windows(process.pid)
                assert not any(title == 'gEngine startup failure' for _, title in found), found
                hwnd = next((handle for handle, title in found if title == 'gEngine GameRunner'), None)
                if hwnd:
                    break
                time.sleep(.05)
            assert hwnd, 'game window missing'
            user.PostMessageW(hwnd, 0x0100, 13, 0)
            user.PostMessageW(hwnd, 0x0101, 13, 0)
            user.PostMessageW(hwnd, 0x0100, 32, 0)
            time.sleep(.5)
            assert (hwnd, 'gEngine GameRunner') in windows(process.pid), 'runtime failed after initialization'
            user.PostMessageW(hwnd, 0x0100, 27, 0)
            user.PostMessageW(hwnd, 0x0101, 27, 0)
            user.PostMessageW(hwnd, 0x0010, 0, 0)
            assert process.wait(timeout=10) == 0
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()

    # 存在しないpackを通常ファイルへ黙って切り替えず、診断表示後に非0で終了する。
    process = subprocess.Popen([str(target), '--pack', str(deployed / 'missing.bpack')], cwd=output)
    try:
        deadline = time.monotonic() + 10
        dialog = None
        while time.monotonic() < deadline:
            dialog = next((handle for handle, title in windows(process.pid) if title == 'gEngine startup failure'), None)
            if dialog and user.IsWindowVisible(dialog):
                break
            time.sleep(.05)
        assert dialog, 'missing pack diagnostic missing'
        user.PostMessageW(dialog, 0x0010, 0, 0)
        assert process.wait(timeout=10) == 1
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
    print('pack-only 2D/3D runner, Japanese path, alternate cwd, input, missing-pack failure passed')


if __name__ == '__main__':
    run(*(Path(value).resolve() for value in sys.argv[1:]))
