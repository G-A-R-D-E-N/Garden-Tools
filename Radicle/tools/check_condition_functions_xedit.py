#!/usr/bin/env python3

import argparse
import json
import re
import sys
from pathlib import Path

ROW = re.compile(r"\(Index:\s*(\d+);\s*Name:\s*'([^']+)'([^)]*)\)")
TABLE = re.compile(r"wbConditionFunctions\s*:\s*array\[[^\]]*\]\s*of\s*TConditionFunction\s*=\s*\(")
RUN_ON = re.compile(r"wbInteger\('Run On',\s*itU32,\s*wbEnum\(\[(.*?)\]\)", re.S)
RUN_ON_ROW = re.compile(r"\{(\d+)\}\s*'([^']+)'")


def xedit_functions(text: str) -> dict:
    start = TABLE.search(text)
    if start is None:
        raise SystemExit("wbConditionFunctions table not found")
    end = text.index(");", start.end())
    rows = {}
    for match in ROW.finditer(text, start.end(), end):
        index = int(match.group(1))
        if index in rows:
            raise SystemExit(f"duplicate xEdit index {index}")
        rows[index] = (match.group(2), len(re.findall(r"ParamType\d", match.group(3), re.I)))
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare schema/fallout4/condition_functions.json with the pinned xEdit tables.")
    parser.add_argument("xedit_definitions", type=Path, help="Core/wbDefinitionsFO4.pas from the pinned xEdit checkout")
    parser.add_argument("--schema", type=Path, default=Path(__file__).resolve().parents[1] / "schema" / "fallout4" / "condition_functions.json")
    args = parser.parse_args()

    text = args.xedit_definitions.read_text(encoding="latin-1")
    schema = json.loads(args.schema.read_text(encoding="utf-8"))
    xedit = xedit_functions(text)
    ours = {row["index"]: (row["name"], len(row["parameters"])) for row in schema["functions"]}

    problems = []
    for index in sorted(set(xedit) | set(ours)):
        if xedit.get(index) != ours.get(index):
            problems.append(f"function {index}: xEdit {xedit.get(index)} schema {ours.get(index)}")

    run_on = RUN_ON.search(text)
    xedit_run_on = {int(v): n for v, n in RUN_ON_ROW.findall(run_on.group(1))} if run_on else {}
    our_run_on = {row["value"]: row["name"] for row in schema["run_on"]}
    if xedit_run_on != our_run_on:
        problems.append(f"run_on: xEdit {xedit_run_on} schema {our_run_on}")

    for line in problems:
        print(line)
    print(f"{len(xedit)} xEdit functions, {len(ours)} schema functions, {len(xedit_run_on)} xEdit run-on values, {len(problems)} differences")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
