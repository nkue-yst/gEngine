"""用途別の配置・移設と、同じ入力のGUI/実stdio MCP制作手順を照合する。"""
import base64
import ctypes
from ctypes import wintypes
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import uuid
from editor_mcp_test import Client
from runner_assets_test import windows, user


def until(condition, reason, seconds=25):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        result = condition()
        if result:
            return result
        time.sleep(.01)
    raise AssertionError(reason)


def key(window, code, down):
    assert user.PostMessageW(window, 0x100 if down else 0x101, code, 0)


def play_view(pid):
    found = []
    callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]

    @callback
    def child(window, _):
        name = ctypes.create_unicode_buffer(128)
        user.GetClassNameW(window, name, len(name))
        if name.value == 'gEnginePlayView':
            found.append(window)
        return True

    user.EnumChildWindows.argtypes = [wintypes.HWND, callback, wintypes.LPARAM]
    for handle, _ in windows(pid):
        child(handle, 0)
        user.EnumChildWindows(handle, child, 0)
    return found[0] if len(found) == 1 else None


def mcp_workflow(editor, root, game, reference):
    name = 'shooter' + game
    client = Client(editor, root)
    report = {}

    def mutate(action, args):
        return client.tool(action, args, uuid.uuid4().hex)

    def target(document):
        return dict(document_id=document['document_id'], expected_revision=document['revision'])

    def capture(play, label):
        response = client.request('tools/call', dict(name='viewport.capture', arguments=play))
        assert response['isError'] is False
        (root / f'{name}-{label}.png').write_bytes(base64.b64decode(response['content'][1]['data']))

    try:
        client.initialize()
        assert len(client.request('tools/list')['tools']) == 50
        project = mutate('project.open', dict(path=f'{name}/project.json'))['project']
        document = mutate('document.open', dict(path='scene.json'))
        document = mutate('scene.set_settings', dict(**target(document), properties={'move_speed': 6.25}))
        document = mutate('history.undo', dict(**target(document), history_id=document['undo_id']))
        report['undo_speed'] = document['scene']['settings']['move_speed']
        document = mutate('history.redo', dict(**target(document), history_id=document['redo_id']))
        document = mutate('document.save', target(document))
        document = mutate('document.reload', target(document))
        assert document['scene']['settings']['move_speed'] == 6.25
        report['scene_hash'] = package_module.digest(root / name / 'scene.json')

        # GUIと同じソース名・不正入力で、診断の位置と文言も比較する。
        source = root / name / 'game.glang'
        original = source.read_bytes()
        source.write_text('export fn main() -> i32 { return true; }\n', encoding='utf-8')
        request = dict(project_id=project['project_id'], expected_revision=project['revision'], source='game.glang')
        failed = client.wait(mutate('compile.start', request)['job_id'])
        assert failed['state'] == 'Failed'
        report['diagnostics'] = failed['diagnostics']
        source.write_bytes(original)
        compiled = client.wait(mutate('compile.start', request)['job_id'])
        assert compiled['state'] == 'Succeeded'

        state = mutate('play.start', dict(job_id=compiled['job_id'], preset=0, game=game, **target(document)))
        play = dict(play_id=state['play_id'])
        assert state['phase'] == 'Start'
        window = until(lambda: play_view(client.process.pid), 'PIE view missing')
        capture(play, 'start')
        key(window, 13, True)
        key(window, 13, False)
        key(window, 32, True)

        def finished():
            state = client.tool('play.get_state', play)
            if state['paused']:
                key(window, ord('P'), True)
                key(window, ord('P'), False)
            key(window, 32, True)
            right = (state['tick'] + 18) % 72 < 36
            key(window, ord('D'), right)
            key(window, ord('A'), not right)
            return state if state['phase'] != 'Playing' else None

        state = until(finished, 'MCP normal loop timed out')
        capture(play, 'result')
        assert state['phase'] == 'Result' and state['outcome'] == 1, state
        report['outcome'] = state['outcome']
        key(window, 27, True)
        key(window, 27, False)
        assert client.tool('play.get_state', play)['phase'] == 'Start'
        capture(play, 'returned')
        assert (root / f'{name}-start.png').read_bytes() == (root / f'{name}-returned.png').read_bytes()
        key(window, 13, True)
        key(window, 13, False)
        assert client.tool('play.get_state', play)['phase'] == 'Playing'
        mutate('play.stop', play)
        assert package_module.digest(root / name / 'scene.json') == report['scene_hash']

        built = client.wait(mutate('build.start', dict(**request, scene='scene.json'))['job_id'])
        assert built['state'] == 'Succeeded', built
        for name_key in ('input_hash', 'artifact_hash', 'artifact'):
            report[name_key] = built[name_key]
        started = mutate('standalone.start', dict(job_id=built['job_id']))
        window = until(lambda: play_view(started['process_id']), 'standalone missing')
        user.PostMessageW(window, 0x10, 0, 0)
        ended = until(lambda: (s if (s := client.tool('standalone.get_state'))['state'] != 'Running' else None),
                      'standalone exit not collected')
        report['natural_exit'] = ended['state']
        report['exit_code'] = ended['exit_code']
        started = mutate('standalone.start', dict(job_id=built['job_id']))
        mutate('standalone.stop', dict(run_id=started['run_id']))
        until(lambda: client.tool('standalone.get_state')['state'] == 'Stopped', 'standalone stop failed')
        report['stopped'] = 'Stopped'
        mutate('project.close', dict(project_id=project['project_id'], expected_revision=project['revision']))
    finally:
        client.close()
    # IDと配置先は別でも、入力・成果物・診断・実行結果は一致しなければならない。
    for name_key in reference.keys() - {'artifact'}:
        assert report[name_key] == reference[name_key], (name_key, report[name_key], reference[name_key])
    (root / f'{name}-mcp.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(game, 'GUI and actual stdio MCP workflow parity passed', flush=True)
    return root / report['artifact']


def hud_matches(frame, reference):
    from PIL import ImageChops, ImageStat

    # 同じ試験で勝利を確認したGPU画像とHUDを照合する。描画サイズの違いだけを正規化する。
    def signature(image):
        channels = image.convert('RGB').crop((0, 0, image.width, image.height // 10)).split()
        white = ImageChops.darker(ImageChops.darker(channels[0], channels[1]), channels[2]).point(
            lambda value: 255 if value >= 180 else 0)
        bounds = white.getbbox()
        assert bounds is not None, 'HUD is not visible'
        return white.crop(bounds).resize((320, 24))

    difference = ImageChops.difference(signature(frame), signature(reference))
    return ImageStat.Stat(difference).mean[0] / 255 < .1


def sample_threats(captured, scale):
    from PIL import ImageChops

    # サンプルの橙色の敵弾を画面から抽出する。大きな敵本体とHUDは対象にしない。
    red, green, blue = captured.convert('RGB').split()
    mask = ImageChops.multiply(red.point(lambda v: 255 if v > 100 else 0),
        ImageChops.multiply(green.point(lambda v: 255 if 45 < v < 230 else 0),
                            ImageChops.subtract(green, blue).point(lambda v: 255 if v > 15 else 0)))
    small = mask.resize((captured.width // scale, captured.height // scale))
    pixels = small.load()
    pending = {(x, y) for y in range(small.height // 10, small.height)
               for x in range(small.width) if pixels[x, y] > 160}
    result = []
    while pending:
        start = pending.pop()
        group = [start]
        stack = [start]
        while stack:
            x, y = stack.pop()
            for point in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if point in pending:
                    pending.remove(point)
                    group.append(point)
                    stack.append(point)
        xs, ys = zip(*group)
        if max(xs) - min(xs) < 14 and max(ys) - min(ys) < 14:
            result.append(((min(xs) + max(xs) + 1) * scale / 2, (min(ys) + max(ys) + 1) * scale / 2))
    return result


def runner_loop(executable, pack, game, root, result_reference):
    # 配布EXEはテスト専用APIを持たないため、通常キーと画面・既存計測で通常一周を観測する。
    from PIL import Image, ImageChops, ImageGrab
    reference = Image.open(result_reference)
    output = root / f'runner-{game}.json'
    trace = []
    process = subprocess.Popen([str(executable), '--game', game, '--pack', str(pack),
                                '--metrics', str(output), '--samples', '3600', '--warmup', '0', '--managed', '1'],
                               cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        window = until(lambda: play_view(process.pid), 'pack-only Runner startup failed')
        user.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int,
                                      ctypes.c_int, ctypes.c_int, wintypes.UINT]
        user.SetForegroundWindow.argtypes = [wintypes.HWND]
        user.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
        user.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
        user.SetWindowPos(window, wintypes.HWND(-1), 20, 20, 980, 600, 0)
        user.SetForegroundWindow(window)

        def frame():
            origin = wintypes.POINT()
            bounds = wintypes.RECT()
            user.ClientToScreen(window, ctypes.byref(origin))
            user.GetClientRect(window, ctypes.byref(bounds))
            return ImageGrab.grab((origin.x, origin.y, origin.x + bounds.right, origin.y + bounds.bottom))

        def screenshot(label):
            time.sleep(.15)
            captured = frame()
            captured.save(root / f'runner-{game}-{label}.png')
            return captured

        def player_bounds(captured):
            # 同梱サンプルの水色の自機を観測し、実際の位置で左右入力を折り返す。
            channels = captured.convert('RGB').split()
            cyan = ImageChops.multiply(channels[0].point(lambda value: 255 if value < 170 else 0),
                ImageChops.multiply(channels[1].point(lambda value: 255 if value > 180 else 0),
                                    channels[2].point(lambda value: 255 if value > 200 else 0)))
            bounds = cyan.getbbox()
            assert bounds is not None, 'sample player is not visible'
            return bounds

        time.sleep(.4)
        initial_frame = screenshot('start')
        assert not hud_matches(initial_frame, reference), 'start screen matched the clear HUD'
        bounds = player_bounds(initial_frame)
        center = (bounds[0] + bounds[2]) / 2
        center_y = (bounds[1] + bounds[3]) / 2
        travel = (bounds[2] - bounds[0]) * 3.5
        key(window, 13, True)
        key(window, 13, False)
        key(window, 32, True)
        # 同じ通常入力で短く右へ移動し、透視投影も含めた画面上の移動方向と速度を測る。
        # 2Dの水平画面座標を3Dへ流用せず、両方とも実際の操作結果から回避を予測する。
        key(window, ord('D'), True)
        time.sleep(.2)
        key(window, ord('D'), False)
        moved = player_bounds(frame())
        dx = (moved[0] + moved[2]) / 2 - center
        dy = (moved[1] + moved[3]) / 2 - center_y
        distance = (dx * dx + dy * dy) ** .5
        assert distance > 2, 'normal movement calibration did not move the player'
        axis = (dx / distance, dy / distance)
        speed = distance / .2
        deadline = time.monotonic() + 30
        started = time.monotonic()
        previous_threats = []
        previous_observed = started
        while True:
            observed = frame()
            if hud_matches(observed, reference):
                break
            if time.monotonic() >= deadline or process.poll() is not None:
                observed.save(root / f'runner-{game}-incomplete.png')
                raise AssertionError('pack-only Runner did not reach the clear HUD')
            bounds = player_bounds(observed)
            position = (bounds[0] + bounds[2]) / 2
            player_y = (bounds[1] + bounds[3]) / 2
            now = time.monotonic()
            elapsed = max(.001, now - previous_observed)
            threats = sample_threats(observed, 2 if game == '3d' else 3)
            predicted = []
            for x, y in threats:
                nearest = min(previous_threats, key=lambda point: (point[0] - x) ** 2 + (point[1] - y) ** 2,
                              default=None)
                # サンプルの敵弾は自機より速い。観測間隔が延びても同じ弾を追跡し、
                # 固定pixel幅を超えた移動を毎回「新しい静止弾」と誤認しない。
                match_distance = max(24, speed * elapsed * 4)
                if nearest is not None and (nearest[0] - x) ** 2 + (nearest[1] - y) ** 2 < match_distance ** 2:
                    predicted.append((x, y, (x - nearest[0]) / elapsed, (y - nearest[1]) / elapsed))
                else:
                    predicted.append((x, y, 0, 0))

            # 固定の往復では弾の到着と折返しの位相で勝敗が変わる。通常の画面情報から
            # 近い将来の接触を避ける横位置を選び、安全な間は敵の正面へ戻って攻撃する。
            position_on_axis = (position - center) * axis[0] + (player_y - center_y) * axis[1]
            radius = max(8, (bounds[2] - bounds[0]) * 1.6) + speed * min(elapsed, .15) / 4

            def risk(target):
                score = abs(target) / travel
                for seconds in (.1, .2, .3, .4, .5, .6):
                    movement = max(-speed * seconds, min(speed * seconds, target - position_on_axis))
                    px = position + axis[0] * movement
                    py = player_y + axis[1] * movement
                    for x, y, vx, vy in predicted:
                        distance = ((px - x - vx * seconds) ** 2 + (py - y - vy * seconds) ** 2) ** .5
                        score += max(0, 1 - distance / radius) ** 2 * 100
                return score

            target = min([travel * scale for scale in (-1, -.5, 0, .5, 1)], key=risk)
            deadzone = max(2, speed * elapsed / 2)
            right = target - position_on_axis > deadzone
            left = position_on_axis - target > deadzone
            trace.append({'seconds': now - started, 'player_x': position, 'player_y': player_y, 'target_on_axis': target,
                          'center_x': center, 'travel': travel, 'axis': axis, 'speed': speed, 'right': right, 'left': left,
                          'threats': predicted})
            previous_threats = threats
            previous_observed = now
            key(window, ord('D'), right)
            key(window, ord('A'), left)
            time.sleep(.03)
        key(window, 32, False)
        key(window, ord('A'), False)
        key(window, ord('D'), False)
        screenshot('result')
        key(window, 27, True)
        key(window, 27, False)
        screenshot('returned')
        # デスクトップ撮影ではWindowsの角丸に背景が合成されるため、外周8pxを比較から除く。
        # 全体画像は保持し、ゲームの開始画面は内側の画素とPIEのGPU画像の両方で照合する。
        initial = Image.open(root / f'runner-{game}-start.png')
        returned = Image.open(root / f'runner-{game}-returned.png')
        assert initial.size == returned.size
        interior = (8, 8, initial.width - 8, initial.height - 8)
        assert ImageChops.difference(initial.crop(interior), returned.crop(interior)).getbbox() is None
        key(window, 13, True)
        key(window, 13, False)
        screenshot('restarted')
        user.PostMessageW(window, 0x10, 0, 0)
        stdout, stderr = process.communicate(timeout=10)
        assert process.returncode == 0, (stdout, stderr)
        report = json.loads(output.read_text(encoding='utf-8'))
        counters = {row['name']: row['value'] for row in report['counters']}
        assert all(counters[key] > 0 for key in ('start_frames', 'playing_frames', 'result_frames')), counters
        # paused_framesは起動・結果・終了時のフォーカス移動も含む総数。
        # 一周の進行は画面と期限で検査し、総数から戦闘中の停止を推測しない。
        assert counters['faulted_frames'] == 0, counters
    finally:
        (root / f'runner-{game}-input.json').write_text(json.dumps(trace), encoding='utf-8')
        if process.poll() is None:
            # 失敗時も通常終了を先に要求し、途中までの計測を診断用JSONへ残す。
            if (window := play_view(process.pid)) is not None:
                user.PostMessageW(window, 0x10, 0, 0)
            try:
                process.communicate(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate()


def run(config, output, gui_test):
    root = output.resolve() / uuid.uuid4().hex
    root.mkdir(parents=True)
    staged = root / '配置 日本語'
    package_module.package(config, staged)
    original = json.loads((staged / 'distribution.json').read_text(encoding='utf-8'))
    try:
        package_module.package(config, staged)
        raise AssertionError('existing destination overwritten')
    except FileExistsError:
        pass
    assert original == json.loads((staged / 'distribution.json').read_text(encoding='utf-8'))

    # この試験が今作ったフォルダーだけを同じ所有領域内で移設する。コピーを増やさない。
    deployed = root / '移設 日本語 空白'
    assert staged.resolve().parent == root and deployed.parent == root and not deployed.exists()
    staged.rename(deployed)
    subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                    str(deployed / 'Initialize-Distribution.ps1')], check=True, cwd=root)
    manifest = json.loads((deployed / 'distribution.json').read_text(encoding='utf-8'))
    assert {p.relative_to(deployed).as_posix() for p in deployed.rglob('*') if p.is_file()} == (
        set(manifest['files']) | {'gEngine.lnk', 'distribution.json'})
    assert all(package_module.digest(deployed / name) == value for name, value in manifest['files'].items())

    # Qt SDKや開発ツールのPATH・Qt環境変数を使わず、別cwdから両入口を使う。
    previous = os.environ.copy()
    previous_cwd = Path.cwd()
    os.environ['PATH'] = os.pathsep.join([str(Path(os.environ['SystemRoot']) / 'System32'), os.environ['SystemRoot']])
    for name in list(os.environ):
        if name.startswith(('QT_', 'QML')):
            del os.environ[name]
    os.chdir(root)
    try:
        editor = deployed / 'Editor/gengine_editor.exe'
        # MCP指定より前の不正引数とworkspace不足も、対話待ちにせず診断で終了する。
        for arguments in (['--unknown', '--mcp', '--workspace', str(root)], ['--mcp']):
            rejected = subprocess.run([str(editor), *arguments], input=b'', capture_output=True, timeout=5)
            assert rejected.returncode == 1 and rejected.stderr and not rejected.stdout
        # ショートカットの実際の起動も通し、生成内容の確認だけで済ませない。
        previous_windows = {handle for handle, _ in windows_any()}
        os.startfile(deployed / 'gEngine.lnk')
        found = until(lambda: next(((h, t) for h, t in windows_any()
                                   if h not in previous_windows and t.startswith('gEngine Editor')), None),
                      'shortcut Editor window missing')
        user.IsWindow.argtypes = [wintypes.HWND]
        user.PostMessageW(found[0], 0x10, 0, 0)
        until(lambda: not user.IsWindow(found[0]), 'shortcut Editor did not close')
        # 単独の言語系は描画・Editorを必要とせず、配置した2本のEXEだけで動く。
        subprocess.run([str(deployed / 'Language/glangc.exe'), str(deployed / 'Language/minimal.glang'),
                        '-o', str(root / 'minimal.glbc')], check=True, capture_output=True)
        vm = subprocess.run([str(deployed / 'Language/gvm.exe'), str(root / 'minimal.glbc')], check=True,
                            capture_output=True, text=True)
        assert 'main returned 0' in vm.stdout

        # pack専用の起動用cmdも別cwdから実行し、配置名に空白があっても起動できる。
        for game in ('2d', '3d'):
            launcher = deployed / f'Games/Play-{game}.cmd'
            previous_windows = {handle for handle, _ in windows_any()}
            launched = subprocess.Popen(['cmd.exe', '/d', '/c', str(launcher)], cwd=root,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                window = until(lambda: next((handle for handle, title in windows_any()
                    if handle not in previous_windows and title == 'gEngine GameRunner'), None), 'game launcher failed')
                user.PostMessageW(window, 0x10, 0, 0)
                stdout, stderr = launched.communicate(timeout=10)
                assert launched.returncode == 0, (stdout, stderr)
            finally:
                if launched.poll() is None:
                    launched.kill()
                    launched.communicate()

        # GUI試験ホストは製品へ混ぜず別フォルダーへ置く。実際に使うツール・素材は移設した製品側。
        host = root / 'GuiHarness'
        host.mkdir()
        shutil.copy2(gui_test, host / gui_test.name)
        shutil.copy2(gui_test.parent / 'Qt6Test.dll', host / 'Qt6Test.dll')
        for name in ('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'qt.conf', 'platforms/qwindows.dll'):
            target = host / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(editor.parent / name, target)
        gui_root = root / 'GUI 制作'
        mcp_root = root / 'MCP 制作'
        for workspace in (gui_root, mcp_root):
            shutil.copytree(deployed / 'Samples', workspace)
        subprocess.run([str(host / gui_test.name), str(gui_root), str(editor.parent / 'glangc.exe')],
                       check=True, timeout=100)
        for game in ('2d', '3d'):
            reference = json.loads((gui_root / f'shooter{game}-gui.json').read_text(encoding='utf-8'))
            product = mcp_workflow(editor, mcp_root, game, reference)
            assert {p.name for p in product.glob('*.exe')} == {'gengine_runner.exe'}
            assert not list(product.rglob('Qt*.dll')) and not (product / 'content').exists()
            runner_loop(product / 'gengine_runner.exe', product / 'game.bpack', game, root,
                        mcp_root / f'shooter{game}-result.png')
        # プラグイン欠落は非0で終了する。OSのエラーダイアログは出さず、診断を保存する。
        plugin = editor.parent / 'platforms/qwindows.dll'
        missing = plugin.with_suffix('.absent')
        plugin.rename(missing)
        try:
            failed = subprocess.run([str(editor), '--mcp', '--workspace', str(root)], input=b'',
                                    capture_output=True, timeout=10, env={**os.environ, 'QT_QPA_PLATFORM': 'windows'})
            assert failed.returncode != 0 and b'platform plugin' in failed.stderr, failed.stderr
            (root / 'missing-plugin.log').write_bytes(failed.stderr)
        finally:
            missing.rename(plugin)
        (root / 'accepted.json').write_text(json.dumps({'distribution': str(deployed), 'version': manifest['version'],
            'configuration': manifest['configuration'], 'gui_mcp_parity': ['2d', '3d'],
            'runner_images_require_review': True}, indent=2), encoding='utf-8')
        print('Distribution relocation acceptance:', root, flush=True)
    finally:
        os.chdir(previous_cwd)
        os.environ.clear()
        os.environ.update(previous)


def windows_any():
    found = []
    callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    @callback
    def visit(handle, _):
        title = ctypes.create_unicode_buffer(512)
        user.GetWindowTextW(handle, title, len(title))
        if user.IsWindowVisible(handle):
            found.append((handle, title.value))
        return True

    user.EnumWindows(visit, 0)
    return found


if __name__ == '__main__':
    spec = importlib.util.spec_from_file_location('package_distribution', Path(__file__).resolve().parents[1] / 'cmake/package_distribution.py')
    package_module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(package_module)
    run(*(Path(value).resolve() for value in sys.argv[1:]))
