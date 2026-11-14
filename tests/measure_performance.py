"""E18の固定条件を独立3プロセスで測定し、同じ入力と事前予算を照合する。"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


def run(command, log, env=None, affinity=False):
    with log.open("w", encoding="utf-8") as output:
        process = subprocess.Popen([str(x) for x in command], stdout=output, stderr=subprocess.STDOUT, env=env)
        try:
            if affinity:
                kernel = ctypes.windll.kernel32
                kernel.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
                assert kernel.SetProcessAffinityMask(int(process._handle), 1 << 8), "CPU affinity failed"
            assert process.wait(timeout=300) == 0, f"measurement failed: {log}"
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()


def records(path):
    return [json.loads(line) for line in path.read_text(encoding="utf-8-sig").splitlines() if line.startswith("{")]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--resume", action="store_true", help="reuse completed update logs and game reports from this unchanged build")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    build = root / "out/build"
    rust = root / "components/gLang/target/release/examples/measure_collections.exe"
    cpp = build / "tests/Release/gengine_update_benchmark.exe"
    play = build / "tests/Release/gengine_performance_play_test.exe"
    runner = build / "Release/gengine_runner.exe"
    provenance = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in [rust, cpp, play, runner]}
    provenance_path = output / "executables.json"
    if args.resume:
        assert json.loads(provenance_path.read_text(encoding="utf-8")) == provenance, "build changed; use a new output"
    else:
        assert not provenance_path.exists(), "use a new output or --resume"
        provenance_path.write_text(json.dumps(provenance, indent=2), encoding="utf-8")
    summary = {"update": [], "games": []}
    corpus = output / "corpus"
    corpus.mkdir(exist_ok=True)
    environment = dict(os.environ, GLANG_MEASURE_CORPUS=str(corpus))
    if not args.resume:
        run([rust, "batch", "C0", 0, 1], output / "corpus.log",
            dict(environment, GLANG_MEASURE_CREATE_CORPUS="1"), affinity=True)
    for repeat in range(1, 4):
        for mode in ["batch", "reference"]:
            if not args.resume:
                run([rust, mode, "C0", 30, 100], output / f"{repeat}-{mode}.jsonl", environment, affinity=True)
        batch = records(output / f"{repeat}-batch.jsonl")
        reference = records(output / f"{repeat}-reference.jsonl")
        for dimension in ["2d", "3d"]:
            sample = next(row for row in batch if row["case"] == f"C0-{dimension}")
            module = corpus / (sample["bytecode_hash"] + ".glbc")
            if not args.resume:
                run([cpp, module, dimension], output / f"{repeat}-cpp-{dimension}.jsonl", affinity=True)
            native = records(output / f"{repeat}-cpp-{dimension}.jsonl")
            for count in [1000, 10000, 100000]:
                width64 = next(row for row in batch if row["case"] == f"C0-{dimension}" and row["count"] == count)
                width1 = next(row for row in reference if row["case"] == f"C0-{dimension}" and row["count"] == count)
                rows = [row for row in native if row["count"] == count]
                assert width1["bytecode_hash"] == width64["bytecode_hash"]
                assert all(row["bytecode_hash"] == int(width64["bytecode_hash"], 16) and row["all_columns_bit_equal"] for row in rows)
                assert width1["allocations"] == width64["allocations"] == 0
                if count == 100000:
                    assert width64["p95_ns"] <= 6_000_000
                    assert next(row for row in rows if row["path"] == "cpp-width64")["p95_ns"] <= 6_000_000
                summary["update"].append({"repeat": repeat, "game": dimension, "count": count,
                                          "width1": width1, "width64": width64, "cpp": rows})
        for dimension in ["2d", "3d"]:
            # フォーカス移動でpauseした標本は性能判定に使わない。元の結果を残し、最大3回まで同条件で採り直す。
            for attempt in range(3):
                target = output / str(repeat) / (dimension if attempt == 0 else f"{dimension}-focus-retry-{attempt}")
                paths = [target / "normal.json", target / "load.json", target / "runner" / (dimension + "-latest.json")]
                if not (args.resume and all(path.exists() for path in paths)):
                    run([play, build / "Release/content", target, dimension], output / f"{repeat}-{dimension}-{attempt}.log")
                    run([sys.executable, root / "tests/runner_performance_test.py", runner, target / "runner", dimension],
                        output / f"{repeat}-runner-{dimension}-{attempt}.log")
                paused = [path for path in paths if any(row["name"] == "paused_frames" and row["value"] > 0
                          for row in json.loads(path.read_text(encoding="utf-8"))["counters"])]
                if not paused:
                    break
                print(f"excluded desktop focus interruption: {paused}", flush=True)
            assert not paused, "desktop focus repeatedly interrupted measurement"
            for path in paths:
                report = json.loads(path.read_text(encoding="utf-8"))
                timing = {row["name"]: row for row in report["timings"]}
                counters = {row["name"]: row["value"] for row in report["counters"]}
                assert report["state"] == "Complete" and report["collected"] == 300
                assert timing["frame"]["p95_ns"] <= 25_000_000 and timing["frame"]["p99_ns"] <= 40_000_000
                assert sum(timing[name]["p95_ns"] for name in ["vm", "contact", "apply"]) <= 2_000_000
                for name, budget in [("conversion", 2_000_000), ("gpu", 6_000_000), ("audio", 1_000_000)]:
                    assert timing[name]["samples"] > 0 and timing[name]["p95_ns"] <= budget, (path, name, timing[name])
                assert report["process_end"]["private_bytes"] <= 256 * 1024 * 1024
                assert counters["faulted_frames"] == 0 and counters["playing_frames"] > 0
                assert counters["paused_frames"] == 0, (path, "desktop focus changed during measurement")
                assert counters["hud_texture_uploads"] == 1, "HUD reuploaded a texture"
                summary["games"].append({"repeat": repeat, "game": dimension, "file": str(path.relative_to(output)),
                                         "frame": timing["frame"], "conversion": timing["conversion"], "gpu": timing["gpu"],
                                         "private_bytes": report["process_end"]["private_bytes"]})
            print(f"accepted repeat={repeat} game={dimension}", flush=True)
    summary["executables"] = provenance
    (output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
