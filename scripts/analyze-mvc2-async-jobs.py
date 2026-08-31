"""Analyze experimental worker wall-time, not exclusive CPU time."""
import argparse
import csv
import json
import re
from pathlib import Path


def distribution(values):
    if not values:
        return dict(count=0)
    values = sorted(values)
    return dict(count=len(values), mean_us=sum(values) / len(values),
                p50_us=values[round((len(values) - 1) * .5)],
                p95_us=values[round((len(values) - 1) * .95)], max_us=values[-1])


def analyze(path):
    with path.open(encoding="utf-8-sig") as source:
        lines = list(source)
    meta = {k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", lines[0])}
    jobs = [{k: int(v) for k, v in row.items()}
            for row in csv.DictReader(line for line in lines if not line.startswith("#"))]
    assert len(jobs) == meta["jobs"], "Truncated job report"
    done = [job for job in jobs if job["ready"]]
    for job in done:
        assert job["queued_us"] <= job["started_us"] <= job["finished_us"]
        assert not job["promoted_us"] or job["finished_us"] <= job["promoted_us"]
    return dict(metadata=meta, ready=len(done),
                queue_wait=distribution([job["started_us"] - job["queued_us"] for job in done]),
                worker_wall=distribution([job["finished_us"] - job["started_us"] for job in done]),
                publish_delay=distribution([job["promoted_us"] - job["finished_us"]
                                            for job in done if job["promoted_us"]]),
                total_worker_wall_ms=sum(job["finished_us"] - job["started_us"] for job in done) / 1000,
                interpreted_entries=sum(job["interpretations"] for job in jobs),
                interpreted_blocks=sum(job["interpretations"] > 0 for job in jobs),
                max_interpretations=max((job["interpretations"] for job in jobs), default=0))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("jobs", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = json.dumps(analyze(args.jobs), indent=2)
    if args.output:
        args.output.write_text(report, encoding="utf-8")
    print(report)
