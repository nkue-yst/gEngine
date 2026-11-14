"""Qtを別cwd・日本語/空白パスへ配置し、差替えたDLLと同梱ソースで起動する。"""
import ctypes
from ctypes import wintypes
import hashlib
import os
from pathlib import Path
import shutil
import sys
import uuid
from editor_mcp_test import Client


def run(executable, workspace, configuration):
    # この試験が所有する配置先だけを更新し、インストール済みQtには触れない。
    deployed = workspace / '日本語 空白' / 'Editor'
    deployed.mkdir(parents=True, exist_ok=True)
    suffix = ''  # 本体の/MD方針に合わせ、Qtは両構成でRelease DLLを使う。
    names = ['gengine_editor.exe', 'glangc.exe', 'qt.conf', 'SOURCE.txt',
             *[f'Qt6{name}{suffix}.dll' for name in ('Core', 'Gui', 'Widgets')],
             f'platforms/qwindows{suffix}.dll', 'backends/rengine_backend_d3d12.dll',
             'backends/aengine_backend_xaudio2.dll', 'licenses/Qt-NOTICES.txt',
             'Sources/qtbase-everywhere-src-6.12.0.tar.xz']
    for name in names:
        target = deployed / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(executable.parent / name, target)
    shutil.copytree(executable.parent / 'content', deployed / 'content', dirs_exist_ok=True)
    source = deployed / names[-1]
    assert hashlib.sha256(source.read_bytes()).hexdigest() == 'a951bd163c7b80fc6b8c88d7668fb56abf91c152373e13c10666763238131307'

    # 公開ABIを維持してPEの私有リソースだけを変え、DLLの固定ハッシュ検査がないことを確認する。
    kernel = ctypes.windll.kernel32
    kernel.BeginUpdateResourceW.argtypes = [wintypes.LPCWSTR, wintypes.BOOL]
    kernel.BeginUpdateResourceW.restype = wintypes.HANDLE
    kernel.UpdateResourceW.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, wintypes.WORD, ctypes.c_void_p, wintypes.DWORD]
    kernel.EndUpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.BOOL]
    library = deployed / f'Qt6Core{suffix}.dll'
    original = hashlib.sha256(library.read_bytes()).digest()
    handle = kernel.BeginUpdateResourceW(str(library), False)
    assert handle
    marker = ctypes.create_string_buffer(b'gEngine Qt replacement acceptance')
    if not kernel.UpdateResourceW(handle, ctypes.c_void_p(10), ctypes.c_void_p(62000), 0, marker, len(marker)):
        kernel.EndUpdateResourceW(handle, True)
        raise ctypes.WinError()
    assert kernel.EndUpdateResourceW(handle, False)
    assert hashlib.sha256(library.read_bytes()).digest() != original

    # インストール先のPATHに頼らず、任意cwdから実体EXEへMCP接続する。
    previous_path = os.environ['PATH']
    previous_cwd = Path.cwd()
    os.environ['PATH'] = os.pathsep.join(part for part in previous_path.split(os.pathsep) if 'qt' not in part.lower())
    os.chdir(workspace)
    client = Client(deployed / executable.name, workspace)
    try:
        client.initialize()
        project = client.tool('project.create', dict(path=uuid.uuid4().hex, template_name='2d'), uuid.uuid4().hex)['project']
        state = client.tool('document.open', dict(path='scene.json'), uuid.uuid4().hex)
        image = client.tool('viewport.capture', dict(document_id=state['document_id'], expected_revision=state['revision']))
        assert image['input_revision'] == state['revision']
        # 通信切断時のdirty文書がモーダルで終了を妨げないことも検証する。
        client.tool('scene.set_settings', dict(document_id=state['document_id'], expected_revision=state['revision'], properties={'move_speed': 7}), uuid.uuid4().hex)
    finally:
        client.close()
        os.chdir(previous_cwd)
        os.environ['PATH'] = previous_path
    print('Qt relocated Japanese/space path, alternate cwd/PATH, modified ABI-compatible DLL, source checksum, dirty EOF passed')


if __name__ == '__main__':
    run(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), sys.argv[3])
