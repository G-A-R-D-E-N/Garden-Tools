#!/usr/bin/env python3
"""Exclude the real-data tests a change cannot affect; design in docs/code-notes/real-data-impact.md."""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_real_data_regex as scan  # noqa: E402

INCLUDE_DIRECTIVE = re.compile(r'^[ \t]*#[ \t]*include\b(.*)$', re.M)
INCLUDE_LITERAL = re.compile(r'^(?:"([^"]+)"|<([^>]+)>)(?:[ \t]*(?://.*|/\*.*\*/)?[ \t]*)$')
CODE_SUFFIXES = {".cpp", ".hpp", ".h", ".cc", ".inl"}
MODULE_SUFFIXES = CODE_SUFFIXES | {".inc"}
EXTERNAL_INCLUDE_PREFIXES = {"catch2", "gtest", "linux", "nlohmann", "reproc++", "spdlog", "sys"}
EXTERNAL_INCLUDE_NAMES = {"aclapi.h", "crtdbg.h", "dirent.h", "dlfcn.h", "fcntl.h", "httplib.h", "malloc.h", "objbase.h", "poll.h",
                         "process.h", "psapi.h", "pthread.h", "sched.h", "signal.h", "time.h",
                         "sddl.h", "shellapi.h", "shobjidl.h", "stddef.h", "stdint.h", "stdlib.h", "unistd.h",
                         "userenv.h", "webview2.h", "winioctl.h", "winternl.h", "windows.h", "wrl.h", "zlib.h"}
DOC_SUFFIXES = {".md", ".txt", ".rst"}
INCLUDE_ROOTS = (ROOT / "Radicle" / "include", ROOT / "Radicle" / "generated", ROOT / "Radicle" / "src", ROOT / "Radicle" / "tests",
                 ROOT / "Radicle" / "tools",
                 ROOT / "PluginEditTool" / "native" / "include", ROOT / "PluginEditTool" / "native" / "src",
                 ROOT / "PluginEditTool" / "native" / "tests", ROOT / "PluginEditTool" / "native" / "apps" / "fo4ide")
SOURCE_TREES = ((ROOT / "Radicle" / "include" / "fo4", ROOT / "Radicle" / "src"),
                (ROOT / "PluginEditTool" / "native" / "include" / "fo4ide", ROOT / "PluginEditTool" / "native" / "src"))
EVERYTHING = (
    r"^cmake/", r"(^|/)CMakeLists\.txt$", r"\.cmake$", r"^CMakePresets\.json$", r"/CMakePresets\.json$",
    r"^\.github/workflows/native-real-data\.yml$", r"^\.github/scripts/", r"^ci/real-data-service/",
    r"^Radicle/schema/", r"^Radicle/tools/", r"^Radicle/testdata/", r"^Radicle/bindings/",
)
SCOPE = ("Radicle/", "PluginEditTool/native/")


def relative(path):
    return path.resolve().relative_to(ROOT).as_posix()


def resolve(name, including, quoted):
    local = including.parent / name
    if quoted and local.is_file():
        return {local.resolve()}
    return {candidate.resolve() for base in INCLUDE_ROOTS
            if (candidate := base / name).is_file()}


def implementation(header):
    for include_dir, src_dir in SOURCE_TREES:
        try:
            rel = header.relative_to(include_dir.resolve())
        except ValueError:
            continue
        candidate = (src_dir / rel).with_suffix(".cpp")
        if candidate.is_file():
            return candidate.resolve()
    sibling = header.with_suffix(".cpp")
    return sibling.resolve() if sibling.is_file() else None


def module(path):
    for include_dir, src_dir in SOURCE_TREES:
        for tree in (include_dir.resolve(), src_dir.resolve()):
            try:
                parts = path.relative_to(tree).parts
            except ValueError:
                continue
            return f"{src_dir.parent.name}:{parts[0]}" if len(parts) > 1 else f"{src_dir.parent.name}:"
    return None


class Includes:
    def __init__(self):
        self.cache = {}
        self.uncertain = False
        self.modules = None

    def of(self, path):
        if path not in self.cache:
            text = path.read_text(encoding="utf-8", errors="replace")
            found = set()
            for directive in INCLUDE_DIRECTIVE.findall(text):
                match = INCLUDE_LITERAL.fullmatch(directive.strip())
                if match is None:
                    self.uncertain = True
                    continue
                quoted, angled = match.groups()
                name = quoted or angled
                candidates = resolve(name, path, quoted is not None)
                found.update(candidates)
                normalized = name.replace("\\", "/")
                parts = normalized.split("/")
                bare_header = (len(parts) == 1 and Path(name).suffix.lower() in {".h", ".hpp", ".inl", ".inc"}
                               and normalized.lower() not in EXTERNAL_INCLUDE_NAMES)
                local = (quoted is not None or
                         bare_header or
                         normalized.startswith(("fo4/", "fo4ide/", "Radicle/", "PluginEditTool/")) or
                         len(parts) > 1 and parts[0] not in EXTERNAL_INCLUDE_PREFIXES)
                if len(candidates) > 1 or (not candidates and local):
                    self.uncertain = True
            self.cache[path] = found
        return self.cache[path]

    def module_files(self):
        if self.modules is None:
            self.modules = {}
            for include_dir, src_dir in SOURCE_TREES:
                for root in (include_dir.resolve(), src_dir.resolve()):
                    for path in root.rglob("*"):
                        if path.is_file() and path.suffix.lower() in MODULE_SUFFIXES:
                            module_id = module(path.resolve())
                            if module_id:
                                self.modules.setdefault(module_id, set()).add(path.resolve())
        return self.modules


def module_closure(start, includes):
    reached = set(start)
    todo = list(reached)
    files = includes.module_files()
    while todo:
        current = todo.pop()
        for path in files.get(current, ()):
            for included in includes.of(path):
                dependency = module(included)
                if dependency and dependency not in reached:
                    reached.add(dependency)
                    todo.append(dependency)
    return reached


def closure(start, includes):
    seen = set()
    todo = [start.resolve()]
    while todo:
        path = todo.pop()
        if path in seen:
            continue
        seen.add(path)
        todo.extend(includes.of(path))
        if path.suffix != ".cpp":
            impl = implementation(path)
            if impl is not None:
                todo.append(impl)
    return seen


def affects_all(path):
    if any(re.search(pattern, path) for pattern in EVERYTHING):
        return True
    if not path.startswith(SCOPE):
        return False
    suffix = Path(path).suffix.lower()
    return ("generated" in path.lower() or suffix == ".inc" or
            suffix not in CODE_SUFFIXES and suffix not in DOC_SUFFIXES)


WORKFLOW = ROOT / ".github" / "workflows" / "native-real-data.yml"


def real_data_regex():
    match = re.search(r"^  REAL_DATA_TEST_REGEX: '((?:[^']|'')*)'\s*$", WORKFLOW.read_text(encoding="utf-8"), re.M)
    if match is None:
        sys.exit(f"::error::REAL_DATA_TEST_REGEX not found in {WORKFLOW}")
    return re.compile(match.group(1).replace("''", "'"))


def real_data_suites():
    """Map each suite the real-data run selects to the test files that define it."""
    regex = real_data_regex()
    suites = {}
    for path in scan.TEST_FILES:
        raw = path.read_text(encoding="utf-8", errors="replace")
        code = scan.blank(raw)
        ids = [(m.group(1) + "." + m.group(2), m.group(1)) for m in scan.GTEST_HEAD.finditer(code)]
        for match in scan.CATCH_HEAD.finditer(code):
            title = raw[match.end():raw.index('"', match.end())]
            ids.append((title, title))
        for test_id, suite in ids:
            if regex.search(test_id):
                suites.setdefault(suite, set()).add(path.resolve())
    return suites


def build_deps(suites):
    includes = Includes()
    deps = {}
    module_files = includes.module_files()
    for files in suites.values():
        for test_file in files:
            if test_file not in deps:
                direct = closure(test_file, includes)
                modules = module_closure({m for m in map(module, direct) if m}, includes)
                deps[test_file] = direct | set().union(*(module_files.get(m, set()) for m in modules))
    return None if includes.uncertain else deps


def affected_suites(changed, suites, deps):
    """Return the suites to run, or None when every test must run."""
    code = []
    for path in changed:
        if affects_all(path):
            return None
        if Path(path).suffix.lower() in CODE_SUFFIXES and not (ROOT / path).is_file():
            return None
        if not path.startswith(SCOPE) or Path(path).suffix.lower() in DOC_SUFFIXES:
            continue
        if (path.startswith(("Radicle/tests/", "PluginEditTool/native/tests/"))):
            code.append(path)
            continue
        changed_module = module((ROOT / path).resolve())
        if changed_module is None or changed_module.endswith(":"):
            return None
        code.append(path)
    if not code:
        return set(), set(suites)
    if deps is None:
        return None
    modules = {test_file: {m for m in map(module, files) if m} for test_file, files in deps.items()}
    changed_files = [(ROOT / path).resolve() for path in code]
    changed_modules = {m for m in map(module, changed_files) if m}
    selected = set()
    for suite, files in suites.items():
        for test_file in files:
            hit = any(path in deps[test_file] for path in changed_files)
            hit = hit or bool(changed_modules & modules[test_file])
            if hit:
                selected.add(suite)
                break
    return selected, set(suites)


GLOBAL_PATHSPECS = ("cmake", "Radicle/schema", "Radicle/tools", "Radicle/testdata", "Radicle/bindings", ".github/scripts",
                    ".github/workflows/native-real-data.yml", "ci/real-data-service", ":(glob)**/CMakeLists.txt",
                    ":(glob)**/*.cmake", ":(glob)**/CMakePresets.json")


def tracked_blobs():
    """Map each tracked path to its blob id; None when git cannot say."""
    result = subprocess.run(["git", "ls-files", "-s"], cwd=ROOT, capture_output=True, text=True, encoding="utf-8")
    if result.returncode != 0:
        return None
    blobs = {}
    for line in result.stdout.splitlines():
        meta, _, name = line.partition("	")
        blobs[name] = meta.split()[1]
    return blobs


def global_digest(blobs):
    digest = hashlib.sha1()
    for name in sorted(blobs):
        if any(fnmatch_spec(name, spec) for spec in GLOBAL_PATHSPECS):
            digest.update(f"{name}:{blobs[name]}\n".encode())
    return digest.hexdigest()


def fnmatch_spec(name, spec):
    if spec.startswith(":(glob)**/"):
        return Path(name).match(spec[len(":(glob)**/"):])
    return name == spec or name.startswith(spec + "/")


def suite_digests(suites, deps, game, context):
    """One digest per suite of every file it can run plus the game data; None when it cannot be trusted."""
    blobs = tracked_blobs()
    if blobs is None or deps is None or not game or not context:
        return None
    shared = hashlib.sha1(("module-graph-v3|" + global_digest(blobs) + "|" + game + "|" + context).encode()).hexdigest()
    digests = {}
    for suite, files in suites.items():
        closure_files = set().union(*(deps[test_file] for test_file in files))
        digest = hashlib.sha1(shared.encode())
        for path in sorted(closure_files):
            name = relative(path)
            blob = blobs.get(name)
            if blob is None:
                return None
            digest.update(f"{name}:{blob}\n".encode())
        digests[suite] = digest.hexdigest()
    return digests


def read_json(path):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError, TypeError):
        return {}


def changed_files(base):
    result = subprocess.run(["git", "diff", "--name-only", "--no-renames", base, "HEAD"], cwd=ROOT,
                            capture_output=True, text=True, encoding="utf-8")
    if result.returncode != 0 or not result.stdout.strip():
        return None
    return result.stdout.split()


def exclude_regex(suites):
    names = sorted(suites)
    plain = "|".join(re.escape(name) for name in names if " " not in name)
    parts = [rf"^({plain})\.", rf"^[^.]*/({plain})\."] if plain else []
    parts.extend("^" + re.escape(name) for name in names if " " in name)
    return "|".join(parts)


def decide(changed, suites, deps, proven=frozenset()):
    """Return (mode, exclude): mode is all, some or none."""
    every = set(suites)
    result = None if changed is None else affected_suites(changed, suites, deps)
    selected = (every if result is None else result[0]) - set(proven)
    skipped = every - selected
    if not selected:
        return "none", ""
    return ("some" if skipped else "all"), exclude_regex(skipped)


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", default="HEAD^1", help="revision to diff HEAD against")
    parser.add_argument("--changed-file", help="file listing changed paths instead of a git diff")
    parser.add_argument("--github-env", default=os.environ.get("GITHUB_ENV"))
    parser.add_argument("--list", action="store_true", help="print each suite as RUN or SKIP")
    args = parser.parse_args(argv)

    suites = real_data_suites()
    deps = build_deps(suites)
    digests = suite_digests(suites, deps, os.environ.get("REAL_DATA_GAME", ""),
                             os.environ.get("REAL_DATA_CONTEXT", ""))
    if digests is not None and os.environ.get("REAL_DATA_HASH_FILE"):
        Path(os.environ["REAL_DATA_HASH_FILE"]).write_text(json.dumps(digests), encoding="utf-8")
    if os.environ.get("REAL_DATA_FULL") == "1":
        print("REAL_DATA_FULL is set; running every real-data test.")
        return 0
    if args.changed_file:
        changed = Path(args.changed_file).read_text(encoding="utf-8").split()
    else:
        changed = changed_files(args.base)
    if changed is None:
        print("::warning::could not determine the changed files; running every real-data test.")
    cache = read_json(os.environ.get("REAL_DATA_CACHE_FILE", "")) if digests is not None else {}
    proven = {suite for suite, digest in (digests or {}).items() if cache.get(suite) == digest}
    mode, exclude = decide(changed, suites, deps, proven)
    if args.list and changed is not None:
        result = affected_suites(changed, suites, deps)
        for suite in sorted(suites):
            if suite in proven:
                print("SKIP " + suite + " (identical code and game data passed before)")
            elif result is not None:
                print(("RUN  " if suite in result[0] else "SKIP ") + suite)
    print(f"real-data impact: {mode}" + (f", {exclude.count('|') + 1} exclusion terms" if exclude else "")
          + (f", {len(proven)} suites already proven" if proven else ""))
    updates = []
    if mode == "none":
        updates.append("REAL_DATA_NO_TESTS=1")
    previous = os.environ.get("CTEST_EXCLUDE_REGEX", "")
    merged = "|".join(part for part in (previous, exclude) if part)
    if merged and merged != previous:
        updates.append(f"CTEST_EXCLUDE_REGEX={merged}")
    if updates and args.github_env:
        with open(args.github_env, "a", encoding="utf-8") as handle:
            handle.write("".join(line + "\n" for line in updates))
    return 0


if __name__ == "__main__":
    sys.exit(main())
