"""実コンパイラーで永続値の型・上限・購入失敗の境界を検査する。"""
from pathlib import Path
import subprocess
import sys

compiler, executable, source, destination = sys.argv[1:]
output = Path(destination)
output.mkdir(parents=True, exist_ok=True)
base = Path(source).read_text(encoding="utf-8")
contract = """
state coins: u32 = 20;
export fn profile_api_version() -> u32 { return 1; }
export fn profile_schema() -> u32 { return 1; }
export fn profile_count() -> u32 { return 1; }
export fn profile_get(index: u32) -> u32 { return coins; }
export fn profile_set(index: u32, value: u32) -> bool { coins = value; return index == 0; }
export fn profile_accept(schema: u32, count: u32) -> bool { return schema == 1 && count == 1 && coins <= 100; }
export fn profile_action_count() -> u32 { return 1; }
export fn profile_action_label(index: u32) -> u32 { return 0; }
export fn profile_action_rank(index: u32) -> u32 { return 0; }
export fn profile_action_maximum(index: u32) -> u32 { return 1; }
export fn profile_action_cost(index: u32) -> u32 { return 5; }
export fn profile_action_enabled(index: u32) -> bool { return coins >= 5; }
export fn profile_action(index: u32) -> bool { coins = coins - 5; return true; }
"""
cases = {
    "valid": (contract, "valid"),
    "reject": (contract.replace("coins = coins - 5; return true;", "coins = coins - 5; return false;"), "reject"),
    "reset": (contract + "export fn profile_reset() -> bool { coins = 0; return true; }", "reset"),
    "reset-reject": (contract + "export fn profile_reset() -> bool { coins = 0; return false; }", "reset-reject"),
    "reset-invalid": (contract + "export fn profile_reset() -> bool { coins = 999; return true; }", "reset-invalid"),
    "version": (contract.replace("profile_api_version() -> u32 { return 1;", "profile_api_version() -> u32 { return 2;"), "unsupported profile"),
    "shape": (contract.replace("profile_count() -> u32 { return 1;", "profile_count() -> u32 { return 33;"), "invalid game profile shape"),
    "action_count": (contract.replace("profile_action_count() -> u32 { return 1;", "profile_action_count() -> u32 { return 5;"), "exceed four"),
    "label": (contract.replace("profile_action_label(index: u32) -> u32 { return 0;", "profile_action_label(index: u32) -> u32 { return 256;"), "invalid profile action"),
    "missing": (contract[:contract.index("export fn profile_action(index")], "ExportNotFound"),
}
for name, (text, expectation) in cases.items():
    path = output / f"{name}.glang"
    module = path.with_suffix(".glbc")
    path.write_text(base + text, encoding="utf-8")
    subprocess.run([compiler, str(path), "-o", str(module)], check=True)
    subprocess.run([executable, str(module), expectation], check=True)
print(f"profile contract: {len(cases)} cases passed")
