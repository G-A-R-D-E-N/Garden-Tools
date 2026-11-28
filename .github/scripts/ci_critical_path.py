import argparse
import json
import os
import subprocess
from datetime import datetime

STAMP = "%Y-%m-%dT%H:%M:%SZ"


def fetch(path):
    done = subprocess.run(["gh", "api", path], capture_output=True, text=True)
    if done.returncode != 0:
        raise RuntimeError(done.stderr.strip()[:200] or "gh api failed")
    return json.loads(done.stdout or "{}")


def minutes(started, finished):
    if not started or not finished:
        return None
    return (datetime.strptime(finished, STAMP) - datetime.strptime(started, STAMP)).total_seconds() / 60.0


def spine(jobs):
    spans = []
    for job in jobs:
        length = minutes(job.get("started_at"), job.get("completed_at"))
        if length is None:
            continue
        spans.append((job.get("started_at"), job.get("completed_at"), length, job.get("name") or "?"))
    if not spans:
        return None
    started = min(span[0] for span in spans)
    finished = max(span[1] for span in spans)
    wall = minutes(started, finished)
    busiest = sum(span[2] for span in spans)
    slowest = max(spans, key=lambda span: span[2])
    return {
        "started": started,
        "finished": finished,
        "wall_minutes": wall,
        "busiest_minutes": busiest,
        "slowest_job": slowest[3],
        "slowest_minutes": slowest[2],
        "parallel_gain_minutes": busiest - wall,
    }


def window_size(limit):
    return min(100, max(limit * 10, 10))


def measure(repository, workflow, limit):
    runs = fetch("repos/" + repository + "/actions/runs?per_page=" + str(window_size(limit))).get("workflow_runs", [])
    rows = []
    for run in runs:
        if len(rows) >= limit:
            break
        if not run.get("conclusion"):
            continue
        if workflow and run.get("name") != workflow:
            continue
        jobs = fetch(
            "repos/" + repository + "/actions/runs/" + str(run.get("id")) + "/jobs?per_page=100"
        ).get("jobs", [])
        shape = spine(jobs)
        if shape is None:
            continue
        row = {
            "run_id": run.get("id"),
            "workflow": run.get("name"),
            "head_sha": (run.get("head_sha") or "")[:8],
            "conclusion": run.get("conclusion"),
        }
        row.update(shape)
        rows.append(row)
    return rows


def table(rows):
    lines = ["| run | result | sha | wall | slowest job | parallel gain |", "|---|---|---|---|---|---|"]
    for row in rows:
        lines.append(
            "| "
            + str(row["run_id"])
            + " | "
            + str(row["conclusion"])
            + " | "
            + str(row["head_sha"])
            + " | "
            + format(row["wall_minutes"], ".0f")
            + " min | "
            + str(row["slowest_job"])
            + " ("
            + format(row["slowest_minutes"], ".0f")
            + " min) | "
            + format(row["parallel_gain_minutes"], ".0f")
            + " min |"
        )
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Measure a lane's wall clock from its job timings.")
    parser.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", ""))
    parser.add_argument("--workflow", default="native-ci")
    parser.add_argument("--limit", type=int, default=5)
    parser.add_argument("--json", default="")
    args = parser.parse_args(argv)
    if not args.repo:
        print("ci-critical-path: no repository")
        return 0
    rows = measure(args.repo, args.workflow, args.limit)
    if not rows:
        print("ci-critical-path: no completed run with job timings")
        return 0
    print(table(rows))
    worst = max(rows, key=lambda row: row["wall_minutes"])
    print(
        "\nworst: "
        + str(worst["run_id"])
        + " at "
        + format(worst["wall_minutes"], ".0f")
        + " min, held open by "
        + str(worst["slowest_job"])
        + " ("
        + format(worst["slowest_minutes"], ".0f")
        + " min)"
    )
    summary = os.environ.get("GITHUB_STEP_SUMMARY", "")
    if summary:
        with open(summary, "a", encoding="utf-8") as handle:
            handle.write(table(rows) + "\n")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump(rows, handle, indent=2)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
