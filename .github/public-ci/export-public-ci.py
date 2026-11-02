#!/usr/bin/env python3
"""Build the commit that public CI runs: one ref's tree minus private files, with no history.

Usage: export-public-ci.py <ref> <message>   (prints the new commit SHA)
"""
import os
import subprocess
import sys
import tempfile
from fnmatch import fnmatchcase
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

DENY = (
    "docs/*",
    "*/docs/*",
    "wiki/*",
    "ci/*",
    "build/*",
    "AGENTS.md",
    "*/AGENTS.md",
    "CLAUDE.md",
    "*/CLAUDE.md",
    "README.md",
    "Radicle/README.md",
    "Radicle/ROADMAP.md",
    ".probe/*",
    "*/.probe/*",
    ".claude/*",
    "*/.claude/*",
    ".codex/*",
    "*/.codex/*",
    ".codex-vendored",
    ".forgejo/*",
    ".github/dependabot.yml",
    ".github/workflows/*",
    ".github/public-ci/*",
    "PluginEditTool/.github/*",
    "PluginEditTool/tools/ci/*",
    ".github/scripts/test_bundled_tools_manifest.py",
    # Game and third-party files; fetch-private-fixtures.sh restores them on the runner.
    "BGS/tools/tests/fixtures/vanilla/Meshes/*",
    "BGS/tools/tests/fixtures/vanilla/README.md",
    "BGS/tools/symrm/samples/*",
    "Radicle/testdata/FW_HeatherSurvivalPatch.esp",
    "Radicle/testdata/SlowTime.esp",
)

ALLOW = ("PluginEditTool/docs/MCP_SETUP.md",)

PUBLIC_WORKFLOWS = ".github/public-ci/workflows/"

GAME_FILES = (".esp", ".esm", ".esl", ".ba2", ".nif", ".hkx", ".dds", ".pex", ".swf", ".wav", ".xwm", ".fuz")


def git(*args, env=None, input=None):
    data = input.encode("utf-8") if input is not None else None
    out = subprocess.run(["git", *args], cwd=ROOT, env=env, input=data, capture_output=True, check=True).stdout
    return out.decode("utf-8")


def exported(path):
    return path in ALLOW or not any(fnmatchcase(path, pattern) for pattern in DENY)


def main():
    ref, message = sys.argv[1], sys.argv[2]
    entries = []
    for line in filter(None, git("ls-tree", "-r", "-z", "--full-tree", ref).split("\0")):
        meta, path = line.split("\t", 1)
        if "\n" in path: raise SystemExit("export refuses a path with a newline")
        mode, kind, sha = meta.split()
        if path.startswith(PUBLIC_WORKFLOWS) and path.endswith(".yml"):
            entries.append(f"100644 {sha}\t.github/workflows/{path[len(PUBLIC_WORKFLOWS):]}")
        elif exported(path):
            entries.append(f"{mode} {sha}\t{path}")
    paths = [entry.split("\t", 1)[1] for entry in entries]
    for required in ("Radicle/src/", "PluginEditTool/native/", "BGS/src/", ".github/workflows/native-ci.yml"):
        if not any(path.startswith(required) for path in paths):
            raise SystemExit(f"export is missing {required}")
    leaked = [path for path in paths if path.lower().endswith(GAME_FILES) or "code-notes" in path
              or path.startswith("docs/") or ("/docs/" in path and path not in ALLOW)]
    if leaked:
        raise SystemExit(f"export would publish private files: {leaked[:5]}")

    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ, GIT_INDEX_FILE=str(Path(tmp) / "index"))
        git("update-index", "--add", "--index-info", env=env, input="\n".join(entries) + "\n")
        tree = git("write-tree", env=env).strip()
    print(git("commit-tree", tree, "-m", message).strip())


if __name__ == "__main__":
    main()
