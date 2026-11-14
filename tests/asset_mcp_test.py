"""実Editorで登録・参照診断・保存を検証する。素材原本と通常操作経路を共用する。"""
import hashlib
import base64
from pathlib import Path
import sys
import subprocess
import time
from runner_assets_test import windows, user
import uuid
from editor_mcp_test import Client


def run(executable, workspace, game):
    workspace.mkdir(parents=True, exist_ok=True)
    client = Client(executable, workspace)
    try:
        client.initialize()
        tools = {entry['name'] for entry in client.request('tools/list')['tools']}
        assert {'asset.list', 'asset.register'} <= tools
        name = '素材 ' + uuid.uuid4().hex
        state = client.tool('project.create', {'path': name, 'template_name': game}, uuid.uuid4().hex)
        project = state['project']
        root = workspace / name
        asset_name = 'sphere.gmesh' if game == '3d' else 'sprite.png'
        asset_path = '日本語 原本.gmesh' if game == '3d' else '日本語 原本.png'
        original = (executable.parent / 'content/assets' / asset_name).read_bytes()
        (root / asset_path).write_bytes(original)
        arguments = dict(project_id=project['project_id'], expected_revision=project['revision'], path=asset_path)
        operation = uuid.uuid4().hex
        result = client.tool('asset.register', arguments, operation)
        assert client.tool('asset.register', arguments, operation) == result
        project = result['project']
        catalog = client.tool('asset.list')
        assert len(catalog['entries']) == 1 and catalog['entries'][0]['kind'] == ('mesh' if game == '3d' else 'image')
        assert not catalog['entries'][0]['diagnostic']
        assert hashlib.sha256((root / asset_path).read_bytes()).digest() == hashlib.sha256(original).digest()
        rejected = client.tool('asset.register', arguments, uuid.uuid4().hex, expect_error=True)
        assert rejected['error']['code'] == 'Conflict'
        (root / asset_path).write_bytes(b'broken')
        assert client.tool('asset.list')['entries'][0]['diagnostic']
        client.tool('project.close', dict(project_id=project['project_id'], expected_revision=project['revision']), uuid.uuid4().hex)
        state = client.tool('project.open', dict(path=project['path']), uuid.uuid4().hex)
        assert state['project']['assets'] == [asset_path]
        project = state['project']
        (root / asset_path).write_bytes(original)
        audio_path = '音声.wav'
        (root / audio_path).write_bytes((executable.parent / 'content/assets/shot.wav').read_bytes())
        project = client.tool('asset.register', dict(project_id=project['project_id'], expected_revision=project['revision'],
                                                   path=audio_path), uuid.uuid4().hex)['project']

        def mutate(action, arguments):
            return client.tool(action, arguments, uuid.uuid4().hex)

        def target():
            current = client.tool('document.get_state')
            return dict(document_id=current['document_id'], expected_revision=current['revision'])

        doc = mutate('document.open', dict(path='scene.json'))
        doc = mutate('scene.set_properties', dict(**target(), object_id='player', properties={'visual': asset_path, 'audio': audio_path}))
        assert doc['dirty'] and doc['undo_id']
        assert len(client.tool('asset.list')['references']) == 2
        doc = mutate('history.undo', dict(**target(), history_id=doc['undo_id']))
        assert doc['scene']['objects'][0]['visual'] is None
        doc = mutate('history.redo', dict(**target(), history_id=doc['redo_id']))
        mutate('document.save', target())
        doc = mutate('document.reload', target())
        assert doc['scene']['objects'][0]['visual'] == asset_path
        accepted = mutate('compile.start', dict(project_id=project['project_id'], expected_revision=project['revision'], source='scripts/game.glang'))
        job = client.wait(accepted['job_id'])
        assert job['state'] == 'Succeeded'

        # 外部破損を対象付きで報告し、編集ビューと文書を保持してプレイだけを拒否する。
        (root / asset_path).write_bytes(b'broken')
        refs = client.tool('asset.list')['references']
        assert refs[0]['object_id'] == 'player' and refs[0]['slot'] == 'visual' and refs[0]['diagnostic']
        doc = mutate('document.reload', target())
        capture = client.request('tools/call', {'name': 'viewport.capture', 'arguments': {**target(), '_operation_id': uuid.uuid4().hex}})
        assert not capture.get('isError'), capture
        picture = next(item for item in capture['content'] if item['type'] == 'image')
        (workspace / ('broken-preview-' + game + '.png')).write_bytes(base64.b64decode(picture['data']))
        failed = client.tool('play.start', dict(job_id=job['job_id'], preset=0, **target()), uuid.uuid4().hex, expect_error=True)
        assert 'player.visual' in failed['error']['reason'], failed
        rejected = client.tool('scene.set_properties', dict(**target(), object_id='player', properties={'visual': 'missing.png', 'audio': None}),
                               uuid.uuid4().hex, expect_error=True)
        assert rejected['error']
        assert client.tool('document.get_state')['scene']['objects'][0]['audio'] == audio_path
        doc = mutate('scene.set_properties', dict(**target(), object_id='player', properties={'visual': None}))
        doc = mutate('history.undo', dict(**target(), history_id=doc['undo_id']))
        assert doc['scene']['objects'][0]['visual'] == asset_path
        (root / asset_path).write_bytes(original)
        assert not client.tool('asset.list')['references'][0]['diagnostic']
        play = mutate('play.start', dict(job_id=job['job_id'], preset=0, **target()))
        assert play['phase'] == 'Start' and play['game'] == game
        mutate('play.stop', dict(play_id=play['play_id']))
        assert client.tool('document.get_state')['scene']['objects'][0]['visual'] == asset_path

        # 未保存の解除も既存の切断復旧を通し、再開後に元の割当てへ戻せる。
        mutate('scene.set_properties', dict(**target(), object_id='player', properties={'audio': None}))
        client.close()
        client = Client(executable, workspace)
        client.initialize()
        project = mutate('project.open', dict(path=project['path']))['project']
        doc = mutate('document.open', dict(path='scene.json'))
        assert doc['recovery_available']
        doc = mutate('document.recover', dict(**target(), choice='restore'))
        assert doc['scene']['objects'][0]['audio'] is None
        mutate('scene.set_properties', dict(**target(), object_id='player', properties={'audio': audio_path}))
        mutate('document.save', target())
        # 保存した参照を--projectでも読み、EXEのcontentだけへ誤解決しないことを確認する。
        accepted = mutate('compile.start', dict(project_id=project['project_id'], expected_revision=project['revision'], source='scripts/game.glang'))
        job = client.wait(accepted['job_id'])
        assert job['state'] == 'Succeeded'
        runner = executable.parent.parent / 'gengine_runner.exe'
        process = subprocess.Popen([str(runner), '--project', str(workspace / project['path']), '--scene', 'scene.json',
                                    '--module', str(workspace / job['artifact'])], cwd=workspace)
        try:
            deadline = time.monotonic() + 10
            hwnd = None
            while time.monotonic() < deadline:
                assert process.poll() is None, 'referenced project runner exited'
                found = windows(process.pid)
                assert not any(title == 'gEngine startup failure' for _, title in found), found
                hwnd = next((handle for handle, title in found if title == 'gEngine GameRunner'), None)
                if hwnd:
                    break
                time.sleep(.05)
            assert hwnd, 'referenced project runner window missing'
            user.PostMessageW(hwnd, 0x0100, 13, 0)
            user.PostMessageW(hwnd, 0x0101, 13, 0)
            user.PostMessageW(hwnd, 0x0100, 32, 0)
            time.sleep(.2)
            user.PostMessageW(hwnd, 0x0010, 0, 0)
            assert process.wait(timeout=10) == 0
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
        print(game, 'MCP asset registration, assignment, history, diagnostics, preview, PIE and recovery passed')
    finally:
        client.close()


if __name__ == '__main__':
    for game in ('2d', '3d'):
        run(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve() / game, game)
