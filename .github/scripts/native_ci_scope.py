#!/usr/bin/env python3
"""Conservatively identify PRs that do not need a native build."""
import json
import os
import re
import subprocess


def file_paths(entries):
    return [path for item in entries for path in (item.get("filename"), item.get("previous_filename")) if path]


def needs_native(paths):
    if not paths:
        return True
    return any(not (path.startswith(("ci/real-data-service/", "docs/"))
                        or path == ".github/workflows/native-real-data.yml") for path in paths)


def command(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def main():
    native = True
    try:
        match = re.fullmatch(r"ci/pr-(\d+)", os.environ.get("GITHUB_REF_NAME", ""))
        if match:
            number = match.group(1)
            message = command("git", "log", "-1", "--format=%B")
            source = re.search(r"(?m)^Source-Commit: ([0-9a-f]{40})$", message)
            base = re.search(r"(?m)^Base-Commit: ([0-9a-f]{40})$", message)
            endpoint = f"repos/G-A-R-D-E-N/GardenTools/pulls/{number}"
            before = json.loads(command("gh", "api", endpoint))
            pages = json.loads(command("gh", "api", "--paginate", "--slurp", endpoint + "/files?per_page=100"))
            after = json.loads(command("gh", "api", endpoint))
            entries = [item for page in pages for item in page]
            # GitHub's files API caps results at 3,000. Never trust truncated,
            # stale, missing or concurrently changed PR information.
            stable = (source and base and before["head"]["sha"] == source.group(1) == after["head"]["sha"]
                      and before["base"]["sha"] == base.group(1) == after["base"]["sha"]
                      and before["changed_files"] == after["changed_files"] == len(entries)
                      and 0 < len(entries) < 3000 and all(item.get("filename") for item in entries))
            if stable:
                native = needs_native(file_paths(entries))
    except (OSError, subprocess.SubprocessError, ValueError, KeyError, TypeError):
        pass
    print("Native builds required" if native else "CI-only PR: native builds not required")
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
        output.write(f"native={'true' if native else 'false'}\n")


if __name__ == "__main__":
    main()
