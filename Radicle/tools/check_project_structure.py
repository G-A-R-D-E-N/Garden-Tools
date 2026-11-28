#!/usr/bin/env python3
"""Fail configure/CI when Radicle's build structure drifts.

Three checks, all read-only:
  lists      every src/**/*.cpp and tests/**/*.cpp is named by the CMake lists
             (platform-specific sources may instead be named by CMakeLists.txt);
  forwarders no new include/fo4/**/*.hpp is a shim whose only content is one
             include, comments and #pragma once;
  cycles     no module-level include cycle outside the known ones. The graph
             reads src/**/*.{cpp,hpp,h} and include/fo4/**/*.{hpp,h}, with
             edges from "fo4/<module>/", root "fo4/<name>.h*" (module "(root)")
             and "../<module>/" includes in angle or quoted form. Top-level src
             files form the "(src)" consumer, and the umbrella include/fo4/fo4.hpp
             is skipped.

The 11 existing forwarders and the 3 existing cycles are baselined so this
guard can land before those cleanups. Remove entries from the baselines as the
files move; a stale baseline entry is harmless, a new violation is not. The
third cycle closes only through the five graph->checks forwarders, so deleting
those shims is what removes it.

Exit codes: 0 clean, 1 violations. Messages use GitHub ::error:: annotations.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

KNOWN_FORWARDERS = frozenset({
    "include/fo4/forms/weather_concept.hpp",
    "include/fo4/graph/check_errors.hpp",
    "include/fo4/graph/conflict_graph.hpp",
    "include/fo4/graph/conflict_kind.hpp",
    "include/fo4/graph/plugin_diff.hpp",
    "include/fo4/graph/reachability.hpp",
    "include/fo4/plugin/container.hpp",
    "include/fo4/plugin/reader.hpp",
    "include/fo4/plugin/record_edit.hpp",
    "include/fo4/plugin/scene_layout.hpp",
    "include/fo4/plugin/visit.hpp",
})

UMBRELLA_HEADER = "include/fo4/fo4.hpp"

KNOWN_CYCLES = (
    frozenset({"environment", "forms"}),
    frozenset({"material", "nif", "previs"}),
    frozenset({"archive", "checks", "environment", "forms", "graph", "plugin"}),
)

SOURCE_REF = re.compile(r'(?:(?<![\w./-])|(?<=\}/))(src/[^"\'\s#]+\.cpp)')
TEST_REF = re.compile(r'(?:(?<![\w./-])|(?<=\}/))(tests/[^"\'\s#]+\.cpp)')
INCLUDE_REF = re.compile(
    r'^\s*#\s*include\s+[<"](?:fo4/(?:([A-Za-z0-9_]+)/|[A-Za-z0-9_]+\.h)|\.\./([A-Za-z0-9_]+)/)'
)
INCLUDE_LINE = re.compile(r'#\s*include\b')
BLOCK_COMMENT = re.compile(r'/\*.*?\*/', re.DOTALL)
LINE_COMMENT = re.compile(r'//.*')


SOURCE_ADDING = re.compile(r'^\s*(?:list\s*\(\s*APPEND|add_executable|add_library|target_sources)\b', re.IGNORECASE)


def annotation(file: str | None, message: str) -> str:
    def escape(text: str, property_value: bool) -> str:
        text = text.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
        return text.replace(":", "%3A").replace(",", "%2C") if property_value else text

    target = f" file={escape(file, True)}" if file is not None else ""
    return f"::error{target}::{escape(message, False)}"


def posix(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def refs(text: str, pattern: re.Pattern[str]) -> set[str]:
    found = set()
    for line in text.splitlines():
        found |= set(pattern.findall(line.split("#", 1)[0]))
    return found


def list_violations(root: Path = ROOT) -> list[str]:
    listed = set()
    for fragment in sorted((root / "cmake").glob("sources_*.cmake")):
        listed |= refs(fragment.read_text(encoding="utf-8"), SOURCE_REF)
    # Platform-specific sources live in CMakeLists.txt, not in a fragment.
    for line in (root / "CMakeLists.txt").read_text(encoding="utf-8").splitlines():
        if SOURCE_ADDING.match(line):
            listed |= refs(line, SOURCE_REF)

    listed_tests = set()
    for fragment in sorted((root / "cmake").glob("tests_*.cmake")):
        listed_tests |= refs(fragment.read_text(encoding="utf-8"), TEST_REF)

    disk = {posix(path, root) for path in (root / "src").rglob("*.cpp")}
    disk_tests = {posix(path, root) for path in (root / "tests").rglob("*.cpp")}

    problems = []
    for path in sorted(disk - listed):
        problems.append(
            annotation(path, f"{path} is not in cmake/sources_*.cmake; "
            "add it to the matching FO4_SOURCES_* set, or to CMakeLists.txt if it is platform-specific.")
        )
    for path in sorted(listed - disk):
        problems.append(
            annotation(path, f"cmake/sources_*.cmake lists {path}, but the file does not exist; "
            "remove the stale entry or restore the file.")
        )
    for path in sorted(disk_tests - listed_tests):
        problems.append(
            annotation(path, f"{path} is not in cmake/tests_*.cmake; "
            "add it to the matching FO4_TESTS_* set, or to FO4_TEST_SUPPORT_SOURCES for shared fixtures.")
        )
    for path in sorted(listed_tests - disk_tests):
        problems.append(
            annotation(path, f"cmake/tests_*.cmake lists {path}, but the file does not exist; "
            "remove the stale entry or restore the file.")
        )
    print(f"lists: {len(disk)} sources, {len(disk_tests)} tests checked")
    return problems


def is_forwarder(text: str) -> bool:
    code = LINE_COMMENT.sub("", BLOCK_COMMENT.sub("", text))
    lines = [line.strip() for line in code.splitlines() if line.strip()]
    includes = [line for line in lines if INCLUDE_LINE.match(line)]
    return len(includes) == 1 and all(line == "#pragma once" or line in includes for line in lines)


def forwarder_violations(root: Path = ROOT) -> list[str]:
    problems = []
    for path in sorted((root / "include" / "fo4").rglob("*.hpp")):
        if not is_forwarder(path.read_text(encoding="utf-8", errors="ignore")):
            continue
        name = posix(path, root)
        if name in KNOWN_FORWARDERS:
            continue
        problems.append(
            annotation(name, f"{name} is a forwarder-only header (one include, nothing but comments and #pragma once); "
            "include the real target header directly and delete this file, or add it to KNOWN_FORWARDERS "
            "only for a deliberate module move.")
        )
    print(f"forwarders: {len(KNOWN_FORWARDERS)} known shims checked")
    return problems


def module_of(path: Path, root: Path) -> str:
    parts = path.relative_to(root).parts
    if parts[0] == "src":
        return parts[1] if len(parts) > 2 else "(src)"
    return parts[2] if len(parts) > 3 else "(root)"


def include_graph(root: Path) -> dict[str, set[str]]:
    graph: dict[str, set[str]] = {}
    files = [path for pattern in ("*.cpp", "*.hpp", "*.h") for path in (root / "src").rglob(pattern)]
    files += [path for pattern in ("*.hpp", "*.h") for path in (root / "include" / "fo4").rglob(pattern)]
    for path in files:
        if posix(path, root) == UMBRELLA_HEADER:
            continue
        module = module_of(path, root)
        for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
            match = INCLUDE_REF.match(line)
            if match is None:
                continue
            target = match.group(1) or match.group(2) or "(root)"
            if target != module:
                graph.setdefault(module, set()).add(target)
    return graph


def strongly_connected(graph: dict[str, set[str]]) -> list[set[str]]:
    index: dict[str, int] = {}
    low: dict[str, int] = {}
    stack: list[str] = []
    on_stack: set[str] = set()
    components: list[set[str]] = []
    counter = 0

    def visit(node: str) -> None:
        nonlocal counter
        index[node] = low[node] = counter
        counter += 1
        stack.append(node)
        on_stack.add(node)
        for nxt in graph.get(node, ()):
            if nxt not in index:
                visit(nxt)
                low[node] = min(low[node], low[nxt])
            elif nxt in on_stack:
                low[node] = min(low[node], index[nxt])
        if low[node] == index[node]:
            component = set()
            while True:
                member = stack.pop()
                on_stack.discard(member)
                component.add(member)
                if member == node:
                    break
            components.append(component)

    for node in sorted(set(graph) | {nxt for edges in graph.values() for nxt in edges}):
        if node not in index:
            visit(node)
    return components


def cycle_path(component: set[str], graph: dict[str, set[str]]) -> list[str]:
    start = sorted(component)[0]

    def walk(node: str, path: list[str]) -> list[str] | None:
        for nxt in sorted(graph.get(node, ())):
            if nxt not in component:
                continue
            if nxt == start:
                return path + [start]
            if nxt not in path:
                found = walk(nxt, path + [nxt])
                if found is not None:
                    return found
        return None

    return walk(start, [start]) or [start, start]


def cycle_violations(root: Path = ROOT) -> list[str]:
    graph = include_graph(root)
    components = [component for component in strongly_connected(graph) if len(component) > 1]
    problems = []
    for component in sorted(components, key=lambda item: sorted(item)):
        if any(component <= known for known in KNOWN_CYCLES):
            continue
        path = " -> ".join(cycle_path(component, graph))
        problems.append(
            annotation(None, f"module include cycle: {path}; move the shared declaration to a leaf module instead of "
            "including across modules. Known cycles are environment<->forms, material<->nif<->previs and "
            "archive<->plugin<->graph<->checks<->forms<->environment.")
        )
    print(f"cycles: {len(components)} checked")
    return problems


def main() -> int:
    problems = list_violations() + forwarder_violations() + cycle_violations()
    for problem in problems:
        print(problem)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
