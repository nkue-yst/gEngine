"""実コンパイラーで契約の正常・異常モジュールを作り、ホストの拒否境界を検証する。"""
import pathlib
import subprocess
import sys

compiler, executable, source, destination = sys.argv[1:]
output = pathlib.Path(destination)
output.mkdir(parents=True, exist_ok=True)
base = pathlib.Path(source).read_text(encoding="utf-8")
contract = """
state pending_choice: bool = true;
export fn interaction_api_version() -> u32 { return 1; }
export fn interaction_request() -> u32 { if pending_choice { return 1; } return 0; }
export fn interaction_count() -> u32 { return 3; }
export fn interaction_option(index: u32) -> u32 { return index; }
export fn interaction_choose(index: u32) -> bool { pending_choice = false; return true; }
"""
fixtures = {
    "valid": (contract, "valid"),
    "version": (contract.replace("version() -> u32 { return 1;", "version() -> u32 { return 2;"), "unsupported"),
    "count": (contract.replace("count() -> u32 { return 3;", "count() -> u32 { return 4;"), "one to three"),
    "duplicate": (contract.replace("return index;", "return 0;"), "duplicate option"),
    "retained": (contract.replace("pending_choice = false;", "pending_choice = true;"), "retained"),
    "missing": (contract[:contract.index("export fn interaction_choose")], "ExportNotFound"),
}
for name, (extension, expected) in fixtures.items():
    text = output / f"{name}.glang"
    module = output / f"{name}.glbc"
    text.write_text(base + extension, encoding="utf-8")
    subprocess.run([compiler, str(text), "-o", str(module)], check=True)
    subprocess.run([executable, str(module), expected], check=True)
print(f"interaction contract: {len(fixtures)} cases passed")
