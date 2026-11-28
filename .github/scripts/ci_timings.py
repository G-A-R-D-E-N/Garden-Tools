import json
import os
import subprocess
from datetime import datetime


def jobs(repository, run_id):
    done = subprocess.run(
        [
            "gh",
            "api",
            "repos/" + repository + "/actions/runs/" + run_id + "/jobs",
            "--paginate",
            "--jq",
            ".jobs[] | {name, conclusion, status, started_at, completed_at, steps: [.steps[]? | {name, started_at, completed_at}]}",
        ],
        capture_output=True,
        text=True,
    )
    if done.returncode != 0:
        raise RuntimeError(done.stderr.strip()[:200] or "gh api failed")
    return [json.loads(line) for line in done.stdout.splitlines() if line.strip()]


def minutes(job):
    started = job.get("started_at")
    finished = job.get("completed_at")
    if not started or not finished:
        return None
    return (
        datetime.strptime(finished, "%Y-%m-%dT%H:%M:%SZ") - datetime.strptime(started, "%Y-%m-%dT%H:%M:%SZ")
    ).total_seconds() / 60.0


def table(rows):
    lines = ["| Job | Result | Minutes |", "|---|---|---|"]
    for job in rows:
        length = minutes(job)
        result = job.get("conclusion") or job.get("status") or "?"
        lines.append("| " + str(job.get("name", "?")) + " | " + str(result) + " | " + ("-" if length is None else format(length, ".1f")) + " |")
    return "\n".join(lines)


def slow_steps(rows, threshold=1.0):
    found = []
    for job in rows:
        for step in job.get("steps") or []:
            length = minutes(step)
            if length is not None and length >= threshold:
                found.append((str(job.get("name", "?")), str(step.get("name", "?")), length))
    return sorted(found, key=lambda entry: -entry[2])


def step_table(found):
    lines = ["| Job | Step | Minutes |", "|---|---|---|"]
    for job, step, length in found:
        lines.append("| " + job + " | " + step + " | " + format(length, ".1f") + " |")
    return "\n".join(lines)


def report(rows):
    text = table(rows)
    steps = slow_steps(rows)
    if steps:
        text += "\n\n" + step_table(steps)
    for job in rows:
        length = minutes(job)
        if length is not None:
            print(format(length, "6.1f") + " min  " + str(job.get("name", "?")))
    for job, step, length in steps:
        print(format(length, "6.1f") + " min  " + job + " / " + step)
    summary = os.environ.get("GITHUB_STEP_SUMMARY", "")
    if summary:
        with open(summary, "a", encoding="utf-8") as handle:
            handle.write(text + "\n")
    return text


def main():
    repository = os.environ.get("GITHUB_REPOSITORY", "")
    run_id = os.environ.get("GITHUB_RUN_ID", "")
    if not repository or not run_id:
        print("ci-timings: no run context")
        return 0
    try:
        rows = jobs(repository, run_id)
    except Exception as error:
        print("::notice::ci-timings: job list unavailable (" + str(error) + ")")
        return 0
    report(rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
