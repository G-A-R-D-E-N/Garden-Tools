#!/usr/bin/env python3
"""Conservatively identify PRs that do not need a native build."""
import json
import os
import re
import subprocess
from pathlib import Path


def file_paths(entries):
    return [path for item in entries for path in (item.get("filename"), item.get("previous_filename")) if path]


def needs_native(paths):
    if not paths:
        return True
    return any(not (path.startswith(("ci/real-data-service/", "docs/", "Radicle/docs/", "PluginEditTool/docs/",
                                     "PluginEditTool/web/"))
                        or path == ".github/workflows/native-real-data.yml") for path in paths)


def test_targets(paths, root=None):
    """Narrow only when every changed path is a mapped Radicle test source."""
    if root is None:
        root = Path.cwd()
    if not paths or any(not re.fullmatch(r"Radicle/tests/[A-Za-z0-9_./-]+\.cpp", path) for path in paths):
        return None
    mapping = {}
    try:
        for group in ("core", "papyrus", "flash", "assets"):
            text = (root / "Radicle" / "cmake" / f"tests_{group}.cmake").read_text(encoding="utf-8")
            match = re.fullmatch(rf"\s*set\(FO4_TESTS_{group.upper()}\s+(.*?)\)\s*", text, re.S)
            if not match:
                return None
            for source in match.group(1).split():
                if not re.fullmatch(r"(?:tests|tools)/[A-Za-z0-9_./-]+\.cpp", source):
                    return None
                mapping.setdefault("Radicle/" + source, set()).add("fo4_tests_" + group)
    except OSError:
        return None
    if any(path not in mapping for path in paths):
        return None
    return sorted(set().union(*(mapping[path] for path in paths)))


def command(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def main():
    native = True
    targets = None
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
                paths = file_paths(entries)
                native = needs_native(paths)
                targets = test_targets(paths) if native else None
    except (OSError, subprocess.SubprocessError, ValueError, KeyError, TypeError):
        pass
    print("Native builds required" if native else "CI-only PR: native builds not required")
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
        output.write(f"native={'true' if native else 'false'}\n")
        output.write("targets=" + (" ".join(targets) if targets else "all") + "\n")


if __name__ == "__main__":
    main()
