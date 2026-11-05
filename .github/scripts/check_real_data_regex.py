#!/usr/bin/env python3
"""Fail when a test that reads FO4_DATA_DIR is not selected by REAL_DATA_TEST_REGEX."""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github" / "workflows" / "native-real-data.yml"
TEST_FILES = sorted(path
                    for root in (ROOT / "Radicle" / "tests", ROOT / "PluginEditTool" / "native" / "tests")
                    for path in root.rglob("*") if path.suffix in {".cpp", ".hpp", ".h"})

READ_DATA_DIR = re.compile(r'getenv\(\s*"FO4_DATA_DIR"\s*\)')
TOKENS = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)
GTEST_HEAD = re.compile(r'\b(?:TEST|TEST_F|TEST_P)\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)')
CATCH_HEAD = re.compile(r'\bTEST_CASE\s*\(\s*"')
FUNCTION_HEAD = re.compile(r'\b(\w+)\s*\([^;{}]*\)\s*(?:const\s*)?(?:override\s*)?(?:noexcept\s*)?\{')
LAMBDA_HEAD = re.compile(r'\b(\w+)\s*=\s*\[[^\[\]]*\]\s*(?:\([^;{}]*\))?\s*(?:mutable\s*)?(?:->\s*[^;{}=]*?)?\{')
CLASS_HEAD = re.compile(r'\b(?:class|struct)\s+(\w+)')
CALL = re.compile(r'\b(\w+)\s*\(')
FIXTURE_HOOKS = {"SetUp", "SetUpTestSuite"}
NOT_HELPERS = {"if", "for", "while", "switch", "catch", "TEST", "TEST_F", "TEST_P", "TEST_CASE"}


def blank(text):
    def repl(match):
        token = match.group(0)
        if token.startswith(("//", "/*")):
            return re.sub(r"[^\n]", " ", token)
        return token[0] + re.sub(r"[^\n]", " ", token[1:-1]) + token[-1]

    return TOKENS.sub(repl, text)


def body_end(code, open_at):
    depth = 0
    for index in range(open_at, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return len(code) - 1


def configured_regex():
    match = re.search(r"^  REAL_DATA_TEST_REGEX: '((?:[^']|'')*)'\s*$", WORKFLOW.read_text(encoding="utf-8"), re.M)
    if match is None:
        sys.exit(f"::error::REAL_DATA_TEST_REGEX not found in {WORKFLOW}")
    return match.group(1).replace("''", "'")


def definitions(raw, code):
    class_heads = [(match.start(), match.group(1)) for match in CLASS_HEAD.finditer(code)]
    found = []
    for pattern in (FUNCTION_HEAD, LAMBDA_HEAD):
        for match in pattern.finditer(code):
            name = match.group(1)
            if name in NOT_HELPERS:
                continue
            end = body_end(code, match.end() - 1)
            owners = [owner for position, owner in class_heads if position < match.start()]
            fixture = owners[-1] if owners and (name == owners[-1] or name in FIXTURE_HOOKS) else None
            reads = READ_DATA_DIR.search(raw[match.end(1):end + 1]) is not None
            found.append((name, fixture, reads, set(CALL.findall(code[match.end(1):end + 1]))))
    return found


def real_data_tests(paths):
    scanned = []
    for path in paths:
        raw = path.read_text(encoding="utf-8", errors="replace")
        scanned.append((path, raw, blank(raw)))
    defs = [definition for _, raw, code in scanned for definition in definitions(raw, code)]
    helpers = set()
    while True:
        new = {name for name, fixture, reads, calls in defs
               if fixture is None and name not in helpers and (reads or calls & helpers)}
        if not new:
            break
        helpers |= new
    fixtures = {fixture for name, fixture, reads, calls in defs
                if fixture is not None and (reads or calls & helpers)}

    found = []
    for path, raw, code in scanned:
        tests = [(match.end(), f"{match.group(1)}.{match.group(2)}", match.group(1))
                 for match in GTEST_HEAD.finditer(code)]
        for match in CATCH_HEAD.finditer(code):
            title_end = raw.index('"', match.end())
            tests.append((match.end(), raw[match.end():title_end], None))
        for header_end, test_id, suite in tests:
            open_at = code.index("{", header_end)
            body_raw = raw[open_at:body_end(code, open_at) + 1]
            body_code = code[open_at:body_end(code, open_at) + 1]
            uses_data = (READ_DATA_DIR.search(body_raw) or suite in fixtures
                         or set(CALL.findall(body_code)) & helpers)
            if uses_data:
                found.append((path, raw.count("\n", 0, header_end) + 1, test_id))
    return found


def main():
    regex = re.compile(configured_regex())
    found = real_data_tests(TEST_FILES)
    if not found:
        sys.exit("::error::no game-data tests found; the test parser is broken")
    missing = []
    for path, line, test_id in found:
        if not regex.search(test_id):
            missing.append(f"{path.relative_to(ROOT).as_posix()}:{line}: {test_id}")
    for entry in missing:
        print(f"::error::not selected by REAL_DATA_TEST_REGEX: {entry}")
    print(f"game-data tests: {len(found)}, not selected by REAL_DATA_TEST_REGEX: {len(missing)}")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
