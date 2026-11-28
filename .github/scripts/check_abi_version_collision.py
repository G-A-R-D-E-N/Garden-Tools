#!/usr/bin/env python3
"""Fail a pull request whose C API header has a malformed ABI major.minor or lowers it below the base branch's.

Requires: run from a checkout of the pull request with the base branch reachable from `origin`
and `GITHUB_BASE_REF` set, or `--base` passed. Exit codes: 0 clear, 1 a problem, 2 could not run.
"""

import argparse
import os
import re
import subprocess
import sys

HEADER = "Radicle/include/fo4/c_api.h"
HEADER_GLOB = "Radicle/include/fo4/c_api*.h"
MINOR_LIMIT = 100000


def read_version(text):
    parts = []
    for name in ("MAJOR", "MINOR"):
        found = re.search(
            r"^[ \t]*#[ \t]*define[ \t]+RADICLE_ABI_" + name + r"[ \t]+(\d+)[uUlL]*[ \t]*(?://[^\r\n]*)?\r?$",
            text, re.MULTILINE)
        if found is None:
            return None
        parts.append(int(found.group(1)))
    return tuple(parts)


IGNORED = re.compile(r"\s*(?:$|/\*|//|\*|#\s*define\s+RADICLE_ABI_(?:MAJOR|MINOR|VERSION)\b)")


def deleted_lines(diff):
    return sum(1 for line in diff.splitlines()
               if line.startswith("-") and not line.startswith("--- ") and not IGNORED.match(line[1:]))


def problems(head, base, changed_headers, deleted=0):
    if head is None:
        return ["{} does not define RADICLE_ABI_MAJOR and RADICLE_ABI_MINOR as plain numbers.".format(HEADER)]
    if head[1] >= MINOR_LIMIT:
        return ["RADICLE_ABI_MINOR {} must stay below {}, or RADICLE_ABI_VERSION overlaps the next major.".format(
            head[1], MINOR_LIMIT)]
    if not changed_headers or base is None:
        return []
    if head < base:
        return ["this pull request edits {} but its ABI {}.{} is below the base branch's {}.{}. "
                "The minor may not go backwards; rebase and keep it at or above the base.".format(
                    ", ".join(changed_headers), head[0], head[1], base[0], base[1])]
    if head == base:
        return ["this pull request edits {} but keeps ABI {}.{}; bump RADICLE_ABI_MINOR (or MAJOR).".format(
            ", ".join(changed_headers), head[0], head[1])]
    if deleted and head[0] <= base[0]:
        return ["this pull request deletes {} non-comment header line(s), which may remove or change a signature or struct layout; "
                "bump RADICLE_ABI_MAJOR above the base branch's {}.".format(deleted, base[0])]
    return []


def git(*arguments):
    return subprocess.run(("git",) + arguments, check=True, capture_output=True, text=True).stdout


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--base", default=os.environ.get("GITHUB_BASE_REF", ""))
    arguments = parser.parse_args(argv)
    if not arguments.base:
        print("::error::GITHUB_BASE_REF is empty; this check only runs on a pull request")
        return 2
    try:
        head = read_version(git("show", "HEAD:" + HEADER))
        git("fetch", "--no-tags", "origin", arguments.base)
        base = read_version(git("show", "FETCH_HEAD:" + HEADER))
        changed = [path for path in
                   git("diff", "--name-only", "FETCH_HEAD...HEAD", "--", HEADER_GLOB).split("\n") if path]
        deleted = deleted_lines(git("diff", "-U0", "--no-color", "FETCH_HEAD...HEAD", "--", HEADER_GLOB))
    except subprocess.CalledProcessError as error:
        print("::error::cannot read the headers ({}); the gate needs them, so this is exit 2, not a pass".format(error))
        return 2
    found = problems(head, base, changed, deleted)
    for problem in found:
        print("::error::{}".format(problem))
    print("ABI: this pull request {}, base branch {}.".format(head, base))
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
