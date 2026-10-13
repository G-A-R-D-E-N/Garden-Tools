#!/usr/bin/env python3
"""Fail when a test that reads FO4_DATA_DIR is not selected by REAL_DATA_TEST_REGEX."""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github" / "workflows" / "native-ci.yml"
TEST_FILES = [*sorted((ROOT / "Radicle" / "tests").glob("*.cpp")),
              *sorted((ROOT / "PluginEditTool" / "native" / "tests").glob("**/*.cpp"))]

READ_DATA_DIR = re.compile(r'getenv\(\s*"FO4_DATA_DIR"\s*\)')
TOKENS = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)
GTEST_HEAD = re.compile(r'\b(?:TEST|TEST_F|TEST_P)\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)')
CATCH_HEAD = re.compile(r'\bTEST_CASE\s*\(\s*"')
FUNCTION_HEAD = re.compile(r'\b(\w+)\s*\([^;{}]*\)\s*(?:const\s*)?(?:override\s*)?(?:noexcept\s*)?\{')
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


def real_data_tests(path):
    raw = path.read_text(encoding="utf-8", errors="replace")
    code = blank(raw)
    helpers = set()
    fixtures = set()
    for match in FUNCTION_HEAD.finditer(code):
        name = match.group(1)
        if name in NOT_HELPERS:
            continue
        body = raw[match.end() - 1:body_end(code, match.end() - 1) + 1]
        if not READ_DATA_DIR.search(body):
            continue
        classes = re.findall(r"\b(?:class|struct)\s+(\w+)", code[:match.start()])
        if name == "SetUp" and classes:
            fixtures.add(classes[-1])
        else:
            helpers.add(name)
    helper_call = re.compile(r"\b(" + "|".join(sorted(helpers)) + r")\s*\(") if helpers else None

    tests = []
    for match in GTEST_HEAD.finditer(code):
        test_id = f"{match.group(1)}.{match.group(2)}"
        tests.append((match.end(), test_id, match.group(1)))
    for match in CATCH_HEAD.finditer(code):
        title_end = raw.index('"', match.end())
        tests.append((match.end(), raw[match.end():title_end], None))

    found = []
    for header_end, test_id, suite in tests:
        open_at = code.index("{", header_end)
        body_code = code[open_at:body_end(code, open_at) + 1]
        body_raw = raw[open_at:body_end(code, open_at) + 1]
        uses_data = (READ_DATA_DIR.search(body_raw) or suite in fixtures
                     or (helper_call and helper_call.search(body_code)))
        if uses_data:
            line = raw.count("\n", 0, header_end) + 1
            found.append((line, test_id))
    return found


def main():
    regex = re.compile(configured_regex())
    total = 0
    missing = []
    for path in TEST_FILES:
        for line, test_id in real_data_tests(path):
            total += 1
            if not regex.search(test_id):
                missing.append(f"{path.relative_to(ROOT).as_posix()}:{line}: {test_id}")
    if total == 0:
        sys.exit("::error::no game-data tests found; the test parser is broken")
    for entry in missing:
        print(f"::error::not selected by REAL_DATA_TEST_REGEX: {entry}")
    print(f"game-data tests: {total}, not selected by REAL_DATA_TEST_REGEX: {len(missing)}")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
