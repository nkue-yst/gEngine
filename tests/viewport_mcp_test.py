"""実Editorのマウス入力とMCPを交互に使い、2D配置の確定・取消しを検証する。"""
import base64
import ctypes
from ctypes import wintypes
from pathlib import Path
import sys
import time
import uuid
from editor_mcp_test import Client


def run(executable, workspace, hold=False):
    workspace.mkdir(parents=True, exist_ok=True)
    client = Client(executable, workspace)
    user = ctypes.windll.user32
    callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback, wintypes.LPARAM]
    user.EnumChildWindows.argtypes = [wintypes.HWND, callback, wintypes.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    user.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
    user.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
    user.GetDlgCtrlID.argtypes = [wintypes.HWND]
    user.GetDlgItem.argtypes = [wintypes.HWND, ctypes.c_int]
    user.GetDlgItem.restype = wintypes.HWND
    def mutate(name, args):
        return client.tool(name, args, uuid.uuid4().hex)
    def state():
        return client.tool('document.get_state')
    def target():
        s = state()
        return dict(document_id=s['document_id'], expected_revision=s['revision'])
    def view_state():
        return client.tool('viewport.get_state', target())
    def objects():
        return {o['kind']: o for o in state()['scene']['objects']}
    def handles():
        parents = []
        @callback
        def top(hwnd, _):
            pid = wintypes.DWORD()
            user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            name = ctypes.create_unicode_buffer(256)
            user.GetClassNameW(hwnd, name, 256)
            if pid.value == client.process.pid and (name.value == 'gEngine.Editor' or name.value.startswith('Qt') and 'QWindow' in name.value):
                parents.append(hwnd)
            return True
        user.EnumWindows(top, 0)
        assert len(parents) == 1
        controls = {}
        @callback
        def child(hwnd, _):
            text = ctypes.create_unicode_buffer(512)
            user.GetWindowTextW(hwnd, text, 512)
            controls[text.value] = hwnd
            return True
        user.EnumChildWindows(parents[0], child, 0)
        return parents[0], controls
    def pixel(world):
        v = view_state()
        rect = wintypes.RECT()
        user.GetClientRect(view, ctypes.byref(rect))
        scale = min(rect.right / 16, rect.bottom / 9)
        unit = scale * 9 / v['height']
        return (round(rect.right / 2 + (world[0] - v['center'][0]) * unit),
                round(rect.bottom / 2 - (world[1] - v['center'][1]) * unit))
    def mouse(message, point, flags=0):
        user.SendMessageW(view, message, flags, (int(point[0]) & 65535) | ((int(point[1]) & 65535) << 16))
    def click(point):
        mouse(0x201, point, 1)
        mouse(0x202, point)
    def capture(name):
        result = client.request('tools/call', dict(name='viewport.capture', arguments=target()))
        assert result.get('isError') is False, result
        image = base64.b64decode(result['content'][1]['data'])
        (workspace / name).write_bytes(image)
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
        project = mutate('project.create', dict(path=uuid.uuid4().hex, template_name='2d'))['project']
        mutate('document.open', dict(path='scene.json'))
        capture('initial.png')
        time.sleep(.3)
        parent, controls = handles()
        view = controls['gEngine GameRunner']
        # 選択の同期と、Xハンドルの仮表示・一回確定。
        click(pixel([0, -3]))
        assert view_state()['selected_id'] == 'player'
        selected = capture('selected.png')
        before = state()
        p = pixel([0, -3])
        mouse(0x201, (p[0] + 44, p[1]), 1)
        mouse(0x200, (p[0] + 74, p[1] - 30), 1)
        assert view_state()['transient']
        assert state()['revision'] == before['revision']
        result = client.tool('viewport.capture', target(), expect_error=True)
        assert result['error']['code'] == 'Busy'
        mouse(0x202, (p[0] + 74, p[1] - 30))
        after = state()
        assert after['revision'] == before['revision'] + 1
        assert objects()['player_spawn']['position'][0] > 0
        assert objects()['player_spawn']['position'][1] == -3
        assert capture('moved.png') != selected
        after = mutate('history.undo', dict(**target(), history_id=after['undo_id']))
        assert objects()['player_spawn']['position'] == [0, -3]
        mutate('history.redo', dict(**target(), history_id=after['redo_id']))
        # 反復編集後もEditorが所有する子ウィンドウを作り直さない。
        kernel = ctypes.windll.kernel32
        kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel.OpenProcess.restype = wintypes.HANDLE
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        user.GetGuiResources.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        process_handle = kernel.OpenProcess(0x400, False, client.process.pid)
        assert process_handle
        def child_windows():
            windows = set()
            @callback
            def collect(hwnd, _):
                windows.add(hwnd)
                return True
            user.EnumChildWindows(parent, collect, 0)
            return windows
        try:
            capture('repeat-before.png')
            resources = tuple(user.GetGuiResources(process_handle, flag) for flag in (0, 1))
            owned_windows = child_windows()
            for iteration in range(24):
                p = pixel(objects()['player_spawn']['position'])
                mouse(0x201, (p[0] + 44, p[1]), 1)
                mouse(0x200, (p[0] + 64, p[1]), 1)
                mouse(0x202, (p[0] + 64, p[1]))
                after = state()
                mutate('history.undo', dict(**target(), history_id=after['undo_id']))
                if iteration == 11:
                    # 最初の12回をウォームアップにし、続く12回で資源の増加を比較する。
                    capture('repeat-before.png')
                    resources = tuple(user.GetGuiResources(process_handle, flag) for flag in (0, 1))
            capture('repeat-after.png')
            assert handles()[1]['gEngine GameRunner'] == view
            after_resources = tuple(user.GetGuiResources(process_handle, flag) for flag in (0, 1))
            assert child_windows() == owned_windows, 'repeated editing changed owned windows'
            print('OS GDI/USER counts (observation only):', resources, after_resources, flush=True)
        finally:
            kernel.CloseHandle(process_handle)
        # Escape、キャプチャー喪失、サイズ変更で文書を変えずに戻る。
        for cancel in ('escape', 'capture', 'resize', 'focus'):
            before = state()
            p = pixel(objects()['player_spawn']['position'])
            mouse(0x201, (p[0] + 16, p[1] - 16), 1)
            mouse(0x200, (p[0] + 40, p[1] - 40), 1)
            assert view_state()['transient']
            if cancel == 'escape':
                user.SendMessageW(view, 0x100, 27, 0)
            elif cancel == 'capture':
                user.SendMessageW(view, 0x215, 0, 0)
            elif cancel == 'focus':
                user.SendMessageW(view, 0x8, 0, 0)
            else:
                user.SetWindowPos(parent, 0, 0, 0, 960, 800, 6)
            assert view_state()['transient'] is False
            assert state()['scene'] == before['scene']
        # 他のMCP編集をドラッグ解放が上書きしない。
        p = pixel(objects()['player_spawn']['position'])
        mouse(0x201, (p[0] + 44, p[1]), 1)
        mouse(0x200, (p[0] + 70, p[1]), 1)
        mutate('scene.set_properties', dict(**target(), object_id='player', properties={'position': [2, -2]}))
        mouse(0x202, (p[0] + 70, p[1]))
        assert objects()['player_spawn']['position'] == [2, -2]
        before = state()
        v = mutate('viewport.set_view', dict(**target(), center=[1, 0], height=18))
        assert v['center'] == [1, 0] and state()['revision'] == before['revision']
        changed = capture('zoomed.png')
        assert changed != selected
        # 同じ表示要求の再送、型と次元の検証。
        args = dict(**target(), center=[0, 0], height=12)
        first = client.tool('viewport.set_view', args, 'repeat-view')
        assert client.tool('viewport.set_view', args, 'repeat-view') == first
        assert client.tool('viewport.set_view', dict(**target(), center=[0], height=1), uuid.uuid4().hex, True)['error']['code'] == 'InvalidArgument'
        mutate('viewport.reset_view', target())
        # 表示の移動・ホイールは文書と履歴を変更しない。
        before = state()
        center = pixel([0, 0])
        mouse(0x207, center, 16)
        mouse(0x200, (center[0] + 20, center[1] + 10), 16)
        mouse(0x208, (center[0] + 20, center[1] + 10))
        assert view_state()['center'] != [0, 0]
        point = wintypes.POINT(*center)
        user.ClientToScreen(view, ctypes.byref(point))
        old_height = view_state()['height']
        mouse(0x20A, (point.x, point.y), 120 << 16)
        assert view_state()['height'] < old_height
        assert state()['revision'] == before['revision'] and state()['undo_id'] == before['undo_id']
        mutate('viewport.reset_view', target())
        # ゲーム用カメラは枠線から選択できる。
        click(pixel([0, 3]))
        top_enemy = pixel([0, 3])
        before = state()
        mouse(0x201, (top_enemy[0], top_enemy[1] + 44), 1)
        mouse(0x200, (top_enemy[0], top_enemy[1] + 60), 1)
        mouse(0x202, (top_enemy[0], top_enemy[1] + 60))
        assert objects()['enemy_spawn']['position'][1] < 3, 'inward Y handle did not move'
        after = state()
        assert after['revision'] == before['revision'] + 1
        mutate('history.undo', dict(**target(), history_id=after['undo_id']))
        click(pixel([8, 0]))
        assert view_state()['selected_id'] == 'camera'
        capture('camera-selected.png')
        # Qt入力欄の保持とGUIからのクリック配置はqt_editor_test.cppで実入力を検証する。
        mutate('scene.set_properties', dict(**target(), object_id='player', properties={'position': [2, -1]}))
        click(pixel([0, 3]))
        user.SendMessageW(view, 0x100, 46, 0)
        assert 'enemy_spawn' not in objects()
        mutate('scene.create_object', dict(**target(), kind='enemy_spawn', properties={'position': [1, 3]}))
        enemy = objects()['enemy_spawn']
        mutate('scene.set_properties', dict(**target(), object_id='player', properties={'position': [enemy['position'][0], -1]}))
        mutate('scene.set_properties', dict(**target(), object_id=enemy['id'], properties={'fire_interval_seconds': 10}))
        mutate('scene.set_settings', dict(**target(), properties={'enemy_hp': 2}))
        # 保存と再読込みで値・ビュー・選択画像が維持される。
        saved = mutate('document.save', target())
        image = capture('placed.png')
        mutate('document.reload', target())
        assert state()['scene'] == saved['scene']
        assert capture('reloaded.png') == image
        # 編集済みシーンでPIEへ入り、表示状態を保って戻る。
        job = mutate('compile.start', dict(project_id=project['project_id'], expected_revision=project['revision'], source='scripts/game.glang'))
        job = client.wait(job['job_id'])
        assert job['state'] == 'Succeeded'
        before_view = view_state()
        play = mutate('play.start', dict(job_id=job['job_id'], preset=0, **target()))
        assert client.tool('viewport.set_view', dict(**target(), center=[0, 0], height=9), uuid.uuid4().hex, True)['error']['code'] == 'Busy'
        time.sleep(.3)
        play_view = handles()[1]['gEngine GameRunner']
        user.SendMessageW(play_view, 0x100, 13, 0)
        user.SendMessageW(play_view, 0x101, 13, 0)
        user.SendMessageW(play_view, 0x100, 32, 0)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            playing = client.tool('play.get_state', dict(play_id=play['play_id']))
            if playing['phase'] == 'Result':
                break
            time.sleep(.03)
        assert playing['phase'] == 'Result' and playing['outcome'] == 1, playing
        user.SendMessageW(play_view, 0x101, 32, 0)
        user.SendMessageW(play_view, 0x100, 27, 0)
        user.SendMessageW(play_view, 0x101, 27, 0)
        assert client.tool('play.get_state', dict(play_id=play['play_id']))['phase'] == 'Start'
        user.SendMessageW(play_view, 0x100, 13, 0)
        user.SendMessageW(play_view, 0x101, 13, 0)
        assert client.tool('play.get_state', dict(play_id=play['play_id']))['phase'] == 'Playing'
        mutate('play.stop', dict(play_id=play['play_id']))
        assert view_state()['center'] == before_view['center'] and view_state()['height'] == before_view['height']
        assert state()['scene'] == saved['scene']
        capture('final.png')
        print('GUI handles, transient capture, cancel, conflict, view, click placement, save and PIE passed', flush=True)
        if hold:
            print('Holding Editor for visual acceptance; create finish.txt in workspace to close.', flush=True)
            while (workspace / 'finish.txt').exists() is False:
                time.sleep(.2)
    finally:
        client.close()


if __name__ == '__main__':
    run(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), '--hold' in sys.argv)
