"""Find SH4 PCs contributing to JIT bursts; this is NOT a fallback allowlist."""
import argparse
import csv
import hashlib
import importlib.util
import json
from bisect import bisect_left
from collections import defaultdict
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "frame_trace", Path(__file__).with_name("analyze-mvc2-frame-trace.py"))
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)


def candidates(events, intervals, minimum_ms=30, compile_ms=10):
    selected = [row for row in intervals
                if row["wall_ms"] >= minimum_ms and row["compile_ms"] >= compile_ms]
    by_pc = defaultdict(lambda: dict(frames=set(), contributions=[], first=True))
    for event in events:
        if event["kind"] != "compile":
            continue
        for row in selected:
            overlap = min(event["end_us"], round(row["end_s"] * 1e6)) - max(
                event["start_us"], round(row["start_s"] * 1e6))
            if overlap <= 0:
                continue
            item = by_pc[event["arg"]]
            item["frames"].add(row["frame"])
            item["contributions"].append(overlap)
            item["first"] &= event["first_pc"]
    result = []
    for pc, item in by_pc.items():
        result.append(dict(pc=f"{pc:08X}", physical_pc=f"{pc & 0x1fffffff:08X}",
            hitch_frames=";".join(str(n) for n in sorted(item["frames"])),
            compile_contributions=len(item["contributions"]),
            compile_overlap_us=sum(item["contributions"]),
            max_compile_overlap_us=max(item["contributions"]),
            all_first_seen=item["first"]))
    return sorted(result, key=lambda row: row["compile_overlap_us"], reverse=True)


def read_late_runs(path):
    # Optional historical cross-reference only: a different build/window can
    # split blocks differently. Missing PCs are UNKNOWN, not zero executions.
    runs = defaultdict(int)
    for line in path.read_text().splitlines():
        fields = line.split()
        if len(fields) == 8 and fields[0] == "block":
            runs[fields[3].upper()] += int(fields[4])
    return runs


def without_native_hits(events):
    hits = defaultdict(list)
    for event in events:
        if event["kind"] == "native_hit":
            hits[event["arg"]].append(event["start_us"])
    for times in hits.values():
        times.sort()
    result = []
    for event in events:
        if event["kind"] == "compile":
            times = hits[event["arg"]]
            index = bisect_left(times, event["start_us"])
            if index < len(times) and times[index] <= event["end_us"]:
                continue
        result.append(event)
    return result


def test():
    events = [dict(kind="compile", arg=0x8c010000, start_us=110, end_us=150),
              dict(kind="compile", arg=0x8c010002, start_us=170, end_us=180),
              dict(kind="compile", arg=0x8c010000, start_us=210, end_us=230)]
    trace.annotate_first_compiles(events)
    rows = [dict(frame=1, start_s=.000120, end_s=.000160, wall_ms=40, compile_ms=20),
            dict(frame=2, start_s=.000160, end_s=.000200, wall_ms=40, compile_ms=2),
            dict(frame=3, start_s=.000200, end_s=.000240, wall_ms=40, compile_ms=20)]
    found = candidates(events, rows)
    assert len(found) == 1 and found[0]["pc"] == "8C010000"
    assert found[0]["compile_overlap_us"] == 50  # exact overlap, not entire scope
    assert found[0]["hitch_frames"] == "1;3" and not found[0]["all_first_seen"]
    assert found[0]["physical_pc"] == "0C010000"
    with_hit = events + [dict(kind="native_hit", arg=0x8c010000, start_us=145, end_us=145)]
    assert without_native_hits(with_hit) == with_hit[1:]
    print("stutter candidates: clipping, identity and threshold tests passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path, nargs="?")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--historical-jit-profile", type=Path)
    parser.add_argument("--cache-misses-only", action="store_true")
    parser.add_argument("--min-wall-ms", type=float, default=30)
    parser.add_argument("--min-compile-ms", type=float, default=10)
    parser.add_argument("--test", action="store_true")
    args = parser.parse_args()
    if args.test:
        test()
        return
    if not args.trace:
        parser.error("trace required")
    output = args.output or args.trace.parent / (args.trace.stem + "-candidates")
    # Reuse the strict timestamp/count/overflow validation before attribution.
    trace.analyze(args.trace, output / "intervals")
    summary = json.loads((output / "intervals/summary.json").read_text())
    if summary["events"].get("native_hit", 0) and not args.cache_misses_only:
        raise ValueError("Use a cold ordinary-JIT trace, with native-cache replay disabled")
    with args.trace.open(encoding="utf-8-sig") as file:
        events = list(csv.DictReader(line for line in file if not line.startswith("#")))
    for event in events:
        for key in ("start_us", "end_us", "arg", "detail"):
            event[key] = int(event[key])
    trace.annotate_first_compiles(events)
    if args.cache_misses_only:
        events = without_native_hits(events)
    intervals = trace.interval_rows(events, "guest_frame", int(summary["metadata"]["duration_us"]))
    found = candidates(events, intervals, args.min_wall_ms, args.min_compile_ms)
    if args.historical_jit_profile:
        runs = read_late_runs(args.historical_jit_profile)
        for row in found:
            row["historical_window_runs"] = runs.get(row["pc"], "unknown")
    with (output / "candidate-pcs.csv").open("w", newline="") as file:
        if found:
            writer = csv.DictWriter(file, fieldnames=found[0].keys())
            writer.writeheader()
            writer.writerows(found)
    metadata = dict(candidate_count=len(found), cache_misses_only=args.cache_misses_only,
        trace_sha256=hashlib.sha256(args.trace.read_bytes()).hexdigest(),
        thresholds=dict(interval_wall_ms=args.min_wall_ms, compile_overlap_ms=args.min_compile_ms),
        caveats=["Wall scopes include preemption, not exclusive CPU time.",
                 "A PC in a compilation burst is not automatically a good interpreter candidate.",
                 "Trace has no FPSCR/source fingerprint: this CSV MUST NOT drive runtime fallback.",
                 "Measure post-birth execution counts and interpreter/JIT costs in the same workload."])
    if args.historical_jit_profile:
        metadata["historical_jit_profile"] = str(args.historical_jit_profile)
        metadata["historical_jit_sha256"] = hashlib.sha256(args.historical_jit_profile.read_bytes()).hexdigest()
        metadata["caveats"].append("Historical run counts use another build/window; only a preliminary cross-reference.")
    (output / "metadata.json").write_text(json.dumps(metadata, indent=2))
    print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    main()
