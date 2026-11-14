"""実Editorのビルド・単独起動を検証し、生成packで共通Runtimeの通常一周を通す。"""
from pathlib import Path
import ctypes
import json
import subprocess
import struct
import sys
import time
import uuid
from editor_mcp_test import Client
from runner_assets_test import windows, user


def until(callback, reason):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        value = callback()
        if value:
            return value
        time.sleep(.03)
    raise AssertionError(reason)


def run(executable, workspace, play_test):
    workspace.mkdir(parents=True, exist_ok=True)
    client = Client(executable, workspace)
    try:
        client.initialize()
        for game in ('2d', '3d'):
            name = 'ビルド 空白 ' + uuid.uuid4().hex
            project = client.tool('project.create', {'path': name, 'template_name': game}, uuid.uuid4().hex)['project']
            request = dict(project_id=project['project_id'], expected_revision=project['revision'],
                           source=project['sources'][0], scene=project['scenes'][0])
            # 登録・割当て済みの日本語参照も、標準素材と共にpackへ取り込む。
            root = workspace / name
            title = '星の試験 / Product ' + game
            (root / 'product.json').write_text(json.dumps(dict(version=1, title=title, executable='Product.exe', icon='game.ico'), ensure_ascii=False), encoding='utf-8')
            png = (executable.parent / 'content/assets/sprite.png').read_bytes()
            (root / 'game.ico').write_bytes(struct.pack('<HHHBBBBHHII', 0, 1, 1, 16, 16, 0, 0, 1, 32, len(png), 22) + png)
            asset = 'sphere.gmesh' if game == '3d' else 'sprite.png'
            reference = '専用 素材' + Path(asset).suffix
            (root / reference).write_bytes((executable.parent / 'content/assets' / asset).read_bytes())
            catalog = client.tool('asset.register', dict(project_id=project['project_id'], expected_revision=project['revision'], path=reference), uuid.uuid4().hex)
            project = client.tool('editor.get_state')['project']
            request['expected_revision'] = project['revision']
            document = client.tool('document.open', {'path': request['scene']}, uuid.uuid4().hex)
            document = client.tool('scene.set_properties', dict(document_id=document['document_id'], expected_revision=document['revision'], object_id='player', properties={'visual': reference}), uuid.uuid4().hex)
            client.tool('build.start', request, uuid.uuid4().hex, expect_error=True)
            request.update(choice='save', document_revision=document['revision'])
            op = uuid.uuid4().hex
            accepted = client.tool('build.start', request, op)
            assert client.tool('build.start', request, op) == accepted, 'replayed build duplicated'
            built = client.wait(accepted['job_id'])
            assert built['state'] == 'Succeeded', built
            product = workspace / built['artifact']
            manifest = json.loads((product / 'build.json').read_text(encoding='utf-8'))
            assert manifest['input_hash'] == built['input_hash']
            assert 'content:project/' + reference in manifest['inputs']
            assert manifest['executable'] == 'Product.exe' and 'content:game.json' in manifest['inputs']
            assert not (product / 'gengine_runner.exe').exists()

            # 次元やpackを指定せず、別cwdから製品の既定入口と日本語タイトルを確認する。
            process = subprocess.Popen([str(product / 'Product.exe')], cwd=workspace)
            try:
                hwnd = until(lambda: next((h for h, name in windows(process.pid) if name == title), None), 'product title missing')
                user.PostMessageW(hwnd, 0x100, 13, 0)
                user.PostMessageW(hwnd, 0x101, 13, 0)
                time.sleep(.1)
                user.PostMessageW(hwnd, 0x10, 0, 0)
                assert process.wait(timeout=10) == 0
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=5)

            # 生成したpackを実描画・通常入力・結果・帰還・再開始の既存受入へ渡す。
            loop_output = root / 'loop'
            subprocess.run([str(play_test), str(executable.parent / 'content' / ('shooter' + game + '.glbc')),
                            str(loop_output), game, str(product / 'game.bpack')], check=True, timeout=35)
            started = client.tool('standalone.start', {'job_id': built['job_id']}, uuid.uuid4().hex)
            assert started['state'] == 'Running'
            pid = started['process_id']
            hwnd = until(lambda: next((h for h, name in windows(pid) if name == title), None), 'runner window missing')
            client.tool('standalone.start', {'job_id': built['job_id']}, uuid.uuid4().hex, expect_error=True)
            user.PostMessageW(hwnd, 0x100, 13, 0)
            user.PostMessageW(hwnd, 0x101, 13, 0)
            user.PostMessageW(hwnd, 0x100, 32, 0)
            time.sleep(.2)
            user.PostMessageW(hwnd, 0x10, 0, 0)
            ended = until(lambda: (s if (s := client.tool('standalone.get_state'))['state'] != 'Running' else None), 'natural exit not collected')
            assert ended['state'] == 'Exited' and ended['exit_code'] == 0, ended

            # 管理下の異常終了も成功扱いにせず、OSの終了コードを回収する。
            started = client.tool('standalone.start', {'job_id': built['job_id']}, uuid.uuid4().hex)
            kernel = ctypes.windll.kernel32
            kernel.OpenProcess.restype = ctypes.c_void_p
            kernel.TerminateProcess.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
            kernel.CloseHandle.argtypes = [ctypes.c_void_p]
            handle = kernel.OpenProcess(1, False, started['process_id'])
            assert handle and kernel.TerminateProcess(handle, 42)
            kernel.CloseHandle(handle)
            failed_run = until(lambda: (s if (s := client.tool('standalone.get_state'))['state'] == 'Failed' else None), 'abnormal exit not collected')
            assert failed_run['exit_code'] == 42, failed_run

            # 単独起動の失敗はダイアログ待ちにならず、診断を標準エラーへ返す。
            failure = subprocess.run([str(product / 'Product.exe'), '--pack', str(product / 'missing.bpack'), '--managed', '1'],
                                     capture_output=True, timeout=10)
            assert failure.returncode == 1 and failure.stderr, 'managed startup failure not reported'

            # 反復起動・停止は新しいrun IDで扱い、古いIDの停止要求は拒否する。
            for _ in range(3):
                started = client.tool('standalone.start', {'job_id': built['job_id']}, uuid.uuid4().hex)
                client.tool('standalone.stop', {'run_id': ended['run_id']}, uuid.uuid4().hex, expect_error=True)
                stopped = client.tool('standalone.stop', {'run_id': started['run_id']}, uuid.uuid4().hex)
                assert stopped['state'] == 'Stopping'
                until(lambda: client.tool('standalone.get_state')['state'] == 'Stopped', 'stop not collected')

            # 保存済み入力を壊した次のビルドで、前の成功を再利用して起動しない。
            (root / project['sources'][0]).write_text('broken source', encoding='utf-8')
            failed = client.wait(client.tool('build.start', request, uuid.uuid4().hex)['job_id'])
            assert failed['state'] == 'Failed' and failed['phase'] == 'Compiling', failed
            client.tool('standalone.start', {'job_id': built['job_id']}, uuid.uuid4().hex, expect_error=True)
            client.tool('project.close', dict(project_id=project['project_id'], expected_revision=project['revision']), uuid.uuid4().hex)
            print(game, 'MCP saved build, custom reference, pack loop, launch/exit/stop/stale rejection passed', flush=True)

        # 通信切断はEditorと所有中GameRunnerを共に終了させる。
        project = client.tool('project.create', {'path': 'shutdown ' + uuid.uuid4().hex, 'template_name': '2d'}, uuid.uuid4().hex)['project']
        built = client.wait(client.tool('build.start', dict(project_id=project['project_id'], expected_revision=project['revision'], source=project['sources'][0], scene=project['scenes'][0]), uuid.uuid4().hex)['job_id'])
        started = client.tool('standalone.start', {'job_id': built['job_id']}, uuid.uuid4().hex)
        kernel = ctypes.windll.kernel32
        kernel.OpenProcess.restype = ctypes.c_void_p
        kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = kernel.OpenProcess(0x100000, False, started['process_id'])
        assert handle
        client.close()
        client = None
        try:
            assert kernel.WaitForSingleObject(handle, 5000) == 0, 'owned runner survived editor EOF'
        finally:
            kernel.CloseHandle(handle)
        print('Editor EOF reclaimed owned GameRunner', flush=True)
    finally:
        if client:
            client.close()


if __name__ == '__main__':
    run(*(Path(value).resolve() for value in sys.argv[1:]))
