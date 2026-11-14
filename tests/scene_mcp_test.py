"""シーン編集のGUI/MCP交互操作と、保存・画像・試遊を実Editorで検証する。"""
import base64
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import sys
import subprocess
import time
import uuid
from editor_mcp_test import Client


def run(executable, workspace, game):
    workspace.mkdir(parents=True, exist_ok=True)
    client = Client(executable, workspace)
    user = ctypes.windll.user32
    callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback, wintypes.LPARAM]
    user.EnumChildWindows.argtypes = [wintypes.HWND, callback, wintypes.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.GetDlgCtrlID.argtypes = [wintypes.HWND]
    user.GetDlgItem.argtypes = [wintypes.HWND, ctypes.c_int]
    user.GetDlgItem.restype = wintypes.HWND
    user.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    def mutate(name, args):
        return client.tool(name, args, uuid.uuid4().hex)
    def handles():
        tops = []
        @callback
        def visit(hwnd, _):
            pid = wintypes.DWORD()
            user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if pid.value == client.process.pid:
                tops.append(hwnd)
            return True
        user.EnumWindows(visit, 0)
        assert tops
        children = {}
        @callback
        def child(hwnd, _):
            text = ctypes.create_unicode_buffer(512)
            user.GetWindowTextW(hwnd, text, len(text))
            children[text.value] = hwnd
            return True
        for parent in tops:
            children.clear()
            user.EnumChildWindows(parent, child, 0)
            if 'gEngine GameRunner' in children:
                return parent, children
        raise AssertionError('Editor controls not found')
    def target(state):
        return dict(document_id=state['document_id'], expected_revision=state['revision'])
    def capture(state, name):
        result = client.request('tools/call', dict(name='viewport.capture', arguments=target(state)))
        assert result.get('isError') is False, result
        image = base64.b64decode(result['content'][1]['data'])
        assert image.startswith(b'\x89PNG\r\n\x1a\n')
        (workspace / name).write_bytes(image)
        assert result['structuredContent']['input_revision'] == state['revision']
        return image
    try:
        client.initialize()
        try:
            import jsonschema
        except ImportError:
            jsonschema = None
        if jsonschema:
            definitions = {item['name']: item for item in client.request('tools/list')['tools']}
            original_request = client.request
            def checked_request(method, params=None):
                if method == 'tools/call':
                    definition = definitions[params['name']]
                    jsonschema.validate(params['arguments'], definition['inputSchema'])
                result = original_request(method, params)
                if method == 'tools/call' and not result.get('isError', False):
                    jsonschema.validate(result['structuredContent'], definition['outputSchema'])
                return result
            client.request = checked_request
        project = mutate('project.create', dict(path=uuid.uuid4().hex, template_name=game))['project']
        state = mutate('document.open', dict(path='scene.json'))
        initial = capture(state, 'initial.png')
        preview_handle = handles()[1]['gEngine GameRunner']
        state = mutate('scene.set_settings', dict(**target(state), properties={'move_speed': 7.5}))
        # Qt部品の実入力・ページ切替えはqt_editor_test.cppで検証する。
        updated = mutate('scene.set_settings', dict(**target(state), properties={'move_speed': 6.25}))
        assert updated['scene']['settings']['move_speed'] == 6.25, updated
        assert updated['revision'] > state['revision']
        stale = client.tool('scene.set_settings', dict(**target(state), properties={'move_speed': 8}), uuid.uuid4().hex, expect_error=True)
        assert stale['error']['code'] == 'Conflict'
        state = mutate('history.undo', dict(**target(updated), history_id=updated['undo_id']))
        assert state['scene']['settings']['move_speed'] == 7.5
        state = mutate('history.redo', dict(**target(state), history_id=state['redo_id']))
        position = [1, -3] if game == '2d' else [1, 2, 4]
        state = mutate('scene.set_properties', dict(**target(state), object_id='player', properties={'position': position}))
        changed = capture(state, 'edited.png')
        assert handles()[1]['gEngine GameRunner'] == preview_handle, 'edit recreated preview window'
        assert changed != initial, 'preview ignored position edit'
        # カメラの各編集項目も、VMなしの描画入力へ個別に反映される。
        camera_changes = ([{'view_size': [20, 12]}] if game == '2d' else
            [{'position': [14, 12, 16]}, {'target': [1, 1, 0]}, {'fov_degrees': 45}])
        for properties in camera_changes:
            state = mutate('scene.set_properties', dict(**target(state), object_id='camera', properties=properties))
            next_image = capture(state, 'camera.png')
            assert next_image != changed, 'preview ignored camera edit'
            changed = next_image
        state = mutate('document.save', target(state))
        state = mutate('document.reload', target(state))
        assert capture(state, 'reloaded.png') == changed, 'saved scene preview changed after reload'
        accepted = mutate('compile.start', dict(project_id=project['project_id'], expected_revision=project['revision'], source='scripts/game.glang'))
        job = client.wait(accepted['job_id'])
        assert job['state'] == 'Succeeded', job
        # 保存済みシーンを別作業ディレクトリから直接起動する。
        runner = subprocess.Popen([str(Path(sys.argv[3]).resolve()),
            '--project', str(workspace / project['path']), '--scene', 'scene.json',
            '--module', str(workspace / job['artifact'])], cwd=workspace)
        try:
            deadline = time.monotonic() + 10
            runner_windows = []
            @callback
            def find_runner(hwnd, _):
                pid = wintypes.DWORD()
                user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                title = ctypes.create_unicode_buffer(256)
                user.GetWindowTextW(hwnd, title, len(title))
                if pid.value == runner.pid and title.value == 'gEngine GameRunner':
                    runner_windows.append(hwnd)
                return True
            while time.monotonic() < deadline and not runner_windows:
                assert runner.poll() is None, 'GameRunner failed to start'
                user.EnumWindows(find_runner, 0)
                time.sleep(.05)
            assert len(runner_windows) == 1, 'GameRunner scene window missing'
            user.PostMessageW(runner_windows[0], 0x0010, 0, 0)
            assert runner.wait(timeout=5) == 0
        finally:
            if runner.poll() is None:
                runner.kill()
                runner.wait()
        play = mutate('play.start', dict(job_id=job['job_id'], preset=0, **target(state)))
        assert play['document_id'] == state['document_id'] and play['input_revision'] == state['revision']
        busy = client.tool('scene.set_settings', dict(**target(state), properties={'move_speed': 9}), uuid.uuid4().hex, expect_error=True)
        assert busy['error']['code'] == 'Busy'
        mutate('play.reset', dict(play_id=play['play_id'], normal=True))
        mutate('play.stop', dict(play_id=play['play_id']))
        assert client.tool('document.get_state')['scene'] == state['scene'], 'play wrote back into document'
        # 通信切断時の未保存入力を次のEditorで復元する。
        state = mutate('scene.set_settings', dict(**target(state), properties={'move_speed': 4.25}))
        client.close()
        client = Client(executable, workspace)
        client.initialize()
        mutate('project.open', dict(path=project['path']))
        state = mutate('document.open', dict(path='scene.json'))
        assert state['recovery_available']
        state = mutate('document.recover', dict(**target(state), choice='restore'))
        assert state['dirty'] and state['scene']['settings']['move_speed'] == 4.25
        state = mutate('document.save', target(state))
        # 初期化exportを持たない旧APIのモジュールには再コンパイルを案内する。
        source_path = workspace / project['path']
        source_path = source_path.parent / 'scripts/game.glang'
        source_path.write_text(source_path.read_text(encoding='utf-8').replace('export fn configure_', 'fn configure_'), encoding='utf-8')
        project = client.tool('editor.get_state')['project']
        accepted = mutate('compile.start', dict(project_id=project['project_id'], expected_revision=project['revision'], source='scripts/game.glang'))
        old_job = client.wait(accepted['job_id'])
        assert old_job['state'] == 'Succeeded'
        rejected = client.tool('play.start', dict(job_id=old_job['job_id'], preset=0, **target(state)), uuid.uuid4().hex, expect_error=True)
        assert 'recompile' in rejected['error']['reason'], rejected
        print(game, 'GUI/MCP scene, image, save, play and recovery passed')
    finally:
        client.close()


if __name__ == '__main__':
    for game in ('2d', '3d'):
        run(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve() / game, game)
