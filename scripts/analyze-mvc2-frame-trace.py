"""Correlate bounded wall-clock trace scopes; never interpret them as GPU/CPU samples."""
import argparse
import csv
import json
import re
from collections import Counter, defaultdict
from bisect import bisect_left
from pathlib import Path

KINDS = ("compile", "cache_clear", "temp_clear", "ram_invalidate", "queue_wait",
         "render_wait", "process", "draw", "present", "texture",
         "decode", "analyse", "reg_alloc", "emit")


def union_us(events, lo, hi):
    spans = sorted((max(lo, e["start_us"]), min(hi, e["end_us"]))
                   for e in events if e["start_us"] < hi and e["end_us"] > lo)
    total, last = 0, lo
    for start, end in spans:
        if end > last:
            total += end - max(last, start)
            last = end
    return total


def percentile(values, fraction):
    values = sorted(values)
    return values[round((len(values) - 1) * fraction)] if values else 0


def annotate_first_compiles(events):
    virtual, physical = set(), set()
    compiles = sorted((e for e in events if e["kind"] == "compile"), key=lambda e: e["start_us"])
    for e in compiles:
        e["first_pc"] = e["arg"] not in virtual
        e["first_physical_pc"] = (e["arg"] & 0x1fffffff) not in physical
        virtual.add(e["arg"])
        physical.add(e["arg"] & 0x1fffffff)
    return dict(events=len(compiles), distinct_virtual_pcs=len(virtual), distinct_physical_pcs=len(physical))


def interval_rows(events, marker, cutoff):
    grouped = defaultdict(list)
    for e in events:
        grouped[e["kind"]].append(e)
    indexed = {}
    for kind, group in grouped.items():
        group.sort(key=lambda e: e["start_us"])
        indexed[kind] = ([e["start_us"] for e in group],
                         max((e["end_us"] - e["start_us"] for e in group), default=0))
    markers = sorted(grouped[marker], key=lambda e: e["start_us"])
    rows = []
    for a, b in zip(markers, markers[1:]):
        lo, hi = a["start_us"], b["start_us"]
        if lo < 1_000_000 or hi > cutoff - 1_000_000:
            continue  # initialization and omitted cross-deadline scopes
        row = dict(frame=b["arg"], start_s=lo / 1e6, end_s=hi / 1e6, wall_ms=(hi-lo)/1000)
        if marker in ("guest_frame", "vblank"):
            # SH4 scheduler ticks at unchanged 200 MHz guest clock.
            row["guest_ms"] = (b["detail"] - a["detail"]) / 200_000
        for kind in KINDS:
            starts, max_duration = indexed.get(kind, ([], 0))
            stop = bisect_left(starts, hi)
            row[kind + "_ms"] = union_us(grouped[kind][bisect_left(starts, lo-max_duration):stop], lo, hi) / 1000
            row[kind + "_starts"] = stop - bisect_left(starts, lo)
            if kind == "compile":
                started = grouped[kind][bisect_left(starts, lo):stop]
                row["compile_first_pc"] = sum(e.get("first_pc", False) for e in started)
                row["compile_first_physical_pc"] = sum(e.get("first_physical_pc", False) for e in started)
        row["block_check_starts"] = sum(lo <= e["start_us"] < hi for e in grouped["block_check"])
        row["dropped"] = sum(lo <= e["start_us"] < hi and not e["detail"] for e in grouped["queue_result"])
        # These overlap one another across lanes, so do not add them together.
        rows.append(row)
    return rows


def analyze(path, output):
    with path.open(encoding="utf-8-sig") as file:
        lines = list(file)
    meta = dict(re.findall(r"(\w+)=(\d+)", "".join(s for s in lines if s.startswith("#"))))
    if meta.get("version") != "1":
        raise ValueError("Unknown trace version")
    if int(meta["emu_overflow"]) or int(meta["render_overflow"]):
        raise ValueError("Trace overflow: missing events prevent reliable attribution")
    if int(meta.get("wrong_lane", 0)):
        raise ValueError("Unexpected producer thread: incomplete trace")
    events = list(csv.DictReader(s for s in lines if not s.startswith("#")))
    for e in events:
        for key in ("start_us", "end_us", "arg", "detail"):
            e[key] = int(e[key])
        if not 0 <= e["start_us"] <= e["end_us"] < int(meta["duration_us"]):
            raise ValueError("Invalid event timestamps")
    counts = Counter(e["lane"] for e in events)
    if counts["emu"] != int(meta["emu_count"]) or counts["render"] != int(meta["render_count"]):
        raise ValueError("Truncated CSV")
    output.mkdir(parents=True, exist_ok=True)
    summary = {"metadata": meta, "events": dict(Counter(e["kind"] for e in events)),
               "compile_identity": annotate_first_compiles(events)}
    for marker in ("guest_frame", "vblank", "presented"):
        rows = interval_rows(events, marker, int(meta["duration_us"]))
        if not rows:
            raise ValueError("No usable intervals for " + marker)
        with (output / (marker + "-intervals.csv")).open("w", newline="") as file:
            writer = csv.DictWriter(file, fieldnames=rows[0].keys())
            writer.writeheader()
            writer.writerows(rows)
        values = [r["wall_ms"] for r in rows]
        threshold = 30 if marker != "presented" else 40
        slow = [r for r in rows if r["wall_ms"] >= threshold]
        summary[marker] = dict(count=len(rows), mean_ms=sum(values)/len(values),
            p50_ms=percentile(values, .5), p95_ms=percentile(values, .95),
            p99_ms=percentile(values, .99), max_ms=max(values), threshold_ms=threshold,
            slow_count=len(slow), slow_with_compile=sum(r["compile_ms"] > 0 for r in slow),
            top=sorted(rows, key=lambda r: r["wall_ms"], reverse=True)[:12])
    totals = {}
    for kind in KINDS:
        selected = [e for e in events if e["kind"] == kind]
        totals[kind] = dict(count=len(selected), total_wall_ms=union_us(selected, 0, int(meta["duration_us"]))/1000,
                            max_wall_ms=max((e["end_us"]-e["start_us"] for e in selected), default=0)/1000)
    summary["scopes"] = totals
    (output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps({k: ({a: b for a, b in v.items() if a != "top"} if isinstance(v, dict) else v)
                      for k, v in summary.items()}, indent=2))


def test():
    events = [dict(start_us=0, end_us=10), dict(start_us=5, end_us=20), dict(start_us=30, end_us=40)]
    assert union_us(events, 3, 35) == 22  # union, cropping, overlapping scopes
    assert union_us(events, 20, 30) == 0
    assert union_us(events, 10, 11) == 1
    assert percentile([1, 3, 2], .5) == 2
    aliases = [dict(kind="compile", arg=pc, start_us=i) for i, pc in enumerate((0x8c010000, 0x0c010000, 0x8c010000))]
    assert annotate_first_compiles(aliases) == dict(events=3, distinct_virtual_pcs=2, distinct_physical_pcs=1)
    assert [e["first_pc"] for e in aliases] == [True, True, False]
    assert [e["first_physical_pc"] for e in aliases] == [True, False, False]
    fixture = [dict(lane="emu", kind="guest_frame", start_us=s, end_us=s, arg=i, detail=i*3333333)
               for i, s in enumerate((1_100_000, 1_120_000, 1_160_000))]
    fixture += [dict(lane="emu", kind="compile", start_us=1_119_000, end_us=1_123_000, arg=0, detail=0),
                dict(lane="render", kind="texture", start_us=1_121_000, end_us=1_123_000, arg=0, detail=0)]
    rows = interval_rows(fixture, "guest_frame", 3_000_000)
    assert [r["compile_ms"] for r in rows] == [1, 3]
    assert [r["compile_starts"] for r in rows] == [1, 0]
    assert rows[1]["texture_ms"] == 2
    assert rows[1]["wall_ms"] == 40
    assert len(interval_rows(fixture, "guest_frame", 2_140_000)) == 1
    print("frame trace analyzer: all boundary/overlap/attribution tests passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path, nargs="?")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--test", action="store_true")
    args = parser.parse_args()
    if args.test:
        test()
    else:
        if not args.trace:
            parser.error("trace required")
        analyze(args.trace, args.output or args.trace.parent / (args.trace.stem + "-analysis"))
