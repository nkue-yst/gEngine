"""採用したQtソースから、Qt Baseのライセンス原文と第三者の帰属表示を抽出する。"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import posixpath
import tarfile

parser = argparse.ArgumentParser(__doc__)
parser.add_argument('archive', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
expected = 'a951bd163c7b80fc6b8c88d7668fb56abf91c152373e13c10666763238131307'
if hashlib.sha256(args.archive.read_bytes()).hexdigest() != expected:
    raise SystemExit('Qt 6.12.0 source SHA-256 mismatch')
args.output.mkdir(parents=True, exist_ok=True)
with tarfile.open(args.archive) as source:
    prefix = 'qtbase-everywhere-src-6.12.0/'
    files = {m.name:m for m in source.getmembers() if m.isfile()}
    def read(name):
        return source.extractfile(files[name]).read().decode('utf-8-sig')
    # 上流の原文は書き換えず、Qt自体と第三者の権利表示をまとめる。
    notices = ['Qt 6.12.0 / Qt Base notices\nCopyright (C) The Qt Company Ltd. and other contributors.\n'
               'gEngine dynamically links Qt Core, Gui, Widgets and the Windows platform plugin under LGPL-3.0.\n'
               'The following upstream notices cover Qt Base, including optional features not used by gEngine.\n']
    included = set()
    def include(name):
        if name not in included:
            notices.append('\n' + '='*72 + '\n' + name.removeprefix(prefix) + '\n\n' + read(name))
            included.add(name)
    for name in sorted(files):
        if name.startswith(prefix+'LICENSES/'):
            include(name)
    for name in sorted(files):
        if not name.endswith('/qt_attribution.json') or not name.startswith(prefix+'src/'):
            continue
        # QtのJSONは複数行文字列を含むため、上流同様にstrictを解除する。
        records=json.loads(read(name), strict=False)
        if isinstance(records,dict): records=[records]
        for record in records:
            notices.append('\n'+'-'*72+'\n'+json.dumps(record,ensure_ascii=False,indent=2)+'\n')
            license_files=record.get('LicenseFile',[])
            if isinstance(license_files,str): license_files=[license_files]
            for license_file in license_files:
                include(posixpath.normpath(str(PurePosixPath(name).parent/license_file)))
    (args.output/'NOTICES.txt').write_text('\n'.join(notices),encoding='utf-8')
print('Qt notices:', (args.output/'NOTICES.txt').stat().st_size, 'bytes')
