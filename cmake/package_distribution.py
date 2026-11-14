"""許可した成果物だけを新規配置し、移設に必要な入力とファイル一覧を固定する。"""
import argparse
import hashlib
import json
from pathlib import Path
import posixpath
import re
import shutil
import subprocess


QT_SOURCE = 'Sources/qtbase-everywhere-src-6.12.0.tar.xz'
QT_HASH = 'a951bd163c7b80fc6b8c88d7668fb56abf91c152373e13c10666763238131307'
BACKENDS = ['backends/aengine_backend_xaudio2.dll', 'backends/rengine_backend_d3d12.dll']
NOTICES = ['ThirdParty/aEngine-LICENSE.txt', 'ThirdParty/rEngine-LICENSE.txt',
           'ThirdParty/BinPackLib-LICENSE.txt', 'ThirdParty/gLang-LICENSE.txt',
           'ThirdParty/nlohmann-json-LICENSE.MIT']
CONTENT = ['shooter2d.glbc', 'shooter3d.glbc', 'scenes/shooter2d.json', 'scenes/shooter3d.json',
           'assets/sprite.png', 'assets/sphere.gmesh', 'assets/shot.wav', 'assets/hit.wav',
           'assets/clear.wav', 'assets/fail.wav']


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def inputs(config):
    source = Path(config['source']).resolve(strict=True)
    editor = Path(config['editor']).resolve(strict=True)
    files = {}

    def add(root, name, target=None):
        # リンクを経由した別領域の取込みと、配置名の衝突を事前に拒否する。
        path = root / name
        relative = target or name
        resolved = path.resolve(strict=True)
        if resolved != path or not resolved.is_relative_to(root) or not path.is_file():
            raise ValueError(f'input must be a regular local file: {path}')
        if relative in files:
            raise ValueError(f'duplicate destination: {relative}')
        files[relative] = path

    for name in ['gengine_editor.exe', 'glangc.exe', 'bpack.exe', 'gengine_runner.exe', 'qt.conf',
                 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'platforms/qwindows.dll',
                 'licenses/Qt-NOTICES.txt', 'SOURCE.txt', QT_SOURCE, *BACKENDS, *NOTICES]:
        add(editor, name, 'Editor/' + name)
    for name in CONTENT:
        add(editor, 'content/' + name, 'Editor/content/' + name)
    add(editor, 'glangc.exe', 'Language/glangc.exe')
    vm = Path(config['gvm']).resolve(strict=True)
    add(vm.parent, vm.name, 'Language/gvm.exe')
    pack = Path(config['pack']).resolve(strict=True)
    add(pack.parent, pack.name, 'Games/samples.bpack')
    for name in ['gengine_runner.exe', *BACKENDS, *NOTICES]:
        add(editor, name, 'Games/' + name)

    # 文書はREADMEとdocs/userの既知ファイルだけ。ソース一式や履歴をコピーしない。
    for name in ['LICENSE', 'README.md', 'docs/user/README.md', 'components/gLang/README.md',
                 'components/gLang/docs/user/language-and-vm.md', 'ThirdParty/Qt/NOTICES.txt',
                 'ThirdParty/Qt/SOURCE.txt', 'ThirdParty/nlohmann/LICENSE.MIT',
                 'components/gLang/samples/cpp/embedding.cpp',
                 'components/gLang/samples/language/embedding.glang',
                 'components/gLang/tests/gvm_integration_test.cpp',
                 'apps/Editor/templates/game.glang']:
        add(source, name)
    for game in ('2d', '3d'):
        for name in ('project.json', 'scene.json', 'game.glang'):
            add(source, f'samples/shooter{game}/{name}', f'Samples/shooter{game}/{name}')
    add(source, 'components/gLang/LICENSE')
    add(source, 'components/gLang/LICENSE', 'Language/LICENSE')
    add(source, 'components/gLang/samples/language/minimal.glang', 'Language/minimal.glang')
    add(source, 'cmake/Initialize-Distribution.ps1', 'Initialize-Distribution.ps1')

    if digest(editor / QT_SOURCE) != QT_HASH:
        raise ValueError('Qt corresponding source checksum mismatch')
    # 内部資料へのリンクや開発機の絶対パスは配置前に止める。コード内の相対パスは維持する。
    for name, path in files.items():
        if path.suffix != '.md':
            continue
        content = path.read_text(encoding='utf-8')
        if re.search(r'docs/development|(?<![A-Za-z0-9])[A-Za-z]:[/\\]|file://', content):
            raise ValueError(f'internal information in documentation: {name}')
        prose = re.sub(r'^```[^\n]*\n.*?^```\s*$', '', content, flags=re.MULTILINE | re.DOTALL)
        for link in re.findall(r'\[[^\]\n]+\]\(([^)]+)\)', prose):
            link = link.split('#', 1)[0]
            if not link or '://' in link:
                continue
            # Windowsのドライブを含むresolveを避け、論理パスだけを正規化する。
            target = posixpath.normpath(posixpath.join(posixpath.dirname(name), link))
            if target not in files:
                raise ValueError(f'document link is not included: {name}: {link}')
    return source, files


def package(config_path, destination):
    config = json.loads(config_path.read_text(encoding='utf-8'))
    source, files = inputs(config)
    destination = destination.absolute()
    if destination.exists():
        raise FileExistsError('destination already exists; choose a new directory')

    # 全入力の確認後にだけ出力を作る。失敗時の部分配置には完了manifestを付けない。
    destination.mkdir(parents=True)
    hashes = {}
    for name, origin in files.items():
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(origin, target)
        hashes[name] = digest(target)
        if hashes[name] != digest(origin):
            raise OSError(f'copy verification failed: {name}')
    for game in ('2d', '3d'):
        name = f'Games/Play-{game}.cmd'
        (destination / name).write_bytes(
            f'@echo off\r\n"%~dp0gengine_runner.exe" --game {game} --pack "%~dp0samples.bpack"\r\n'.encode('ascii'))
        hashes[name] = digest(destination / name)

    # ショートカットは配置先依存のためハッシュ対象外。移設後は同じスクリプトで再生成する。
    subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                    str(destination / 'Initialize-Distribution.ps1')], check=True)
    # 検証中の未コミット変更を、採用済みHEADからの配置と取り違えないよう記録する。
    revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    changed = subprocess.run(['git', '-C', str(source), 'diff', '--quiet', '--ignore-submodules=untracked', 'HEAD'])
    if changed.returncode not in (0, 1):
        raise RuntimeError('cannot inspect source revision')
    manifest = {'format_version': 1, 'version': config['version'], 'configuration': config['configuration'],
                'source_revision': revision, 'source_modified': changed.returncode == 1, 'files': dict(sorted(hashes.items())),
                'regenerated_files': ['gEngine.lnk']}
    (destination / 'distribution.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'directory': str(destination), 'files': len(hashes),
                      'bytes': sum((destination / name).stat().st_size for name in hashes)}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('config', type=Path)
    parser.add_argument('destination', type=Path)
    options = parser.parse_args()
    package(options.config, options.destination)
