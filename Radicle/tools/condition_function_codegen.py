#!/usr/bin/env python3

import argparse
import json
import re
from pathlib import Path

KINDS = {
    "none": "none",
    "integer": "integer",
    "float": "float_value",
    "string": "string",
    "form_link": "form_link",
    "enum": "enum_value",
    "other": "other",
}

MAX_INDEX = 0xFFFF
MAX_PARAMETERS = 3
MAX_RUN_ON_VALUE = 0xFFFFFFFF
PARAMETER_TYPE_NAME = re.compile(r"^[a-z][a-z0-9_]*$")
FUNCTION_NAME = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")
RUN_ON_NAME = re.compile(r"^[A-Za-z][A-Za-z0-9 ]*$")


def valid_signature(value) -> bool:
    return isinstance(value, str) and len(value) == 4 and all(0x20 <= ord(ch) <= 0x7E for ch in value)


def cpp_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def load_parameter_types(entries: list) -> list:
    parsed = []
    seen = set()
    for entry in entries:
        if not isinstance(entry, dict) or not set(entry) <= {"name", "kind", "targets"}:
            raise ValueError("parameter_types entries must be objects with name, kind and optional targets")
        name = entry.get("name")
        kind = entry.get("kind")
        if not isinstance(name, str) or not PARAMETER_TYPE_NAME.match(name):
            raise ValueError(f"parameter type name must be snake_case: {name!r}")
        if name in seen:
            raise ValueError(f"duplicate parameter type: {name}")
        seen.add(name)
        if kind not in KINDS:
            raise ValueError(f"unsupported parameter kind for {name}: {kind!r}")
        if kind == "form_link":
            targets = entry.get("targets")
            if not isinstance(targets, list) or not targets:
                raise ValueError(f"form_link parameter type needs targets: {name}")
            for target in targets:
                if not valid_signature(target):
                    raise ValueError(f"target is not a record signature for {name}: {target!r}")
            if len(set(targets)) != len(targets):
                raise ValueError(f"duplicate target for {name}")
            parsed.append({"name": name, "kind": kind, "targets": list(targets)})
        else:
            if "targets" in entry:
                raise ValueError(f"only form_link parameter types may have targets: {name}")
            parsed.append({"name": name, "kind": kind, "targets": []})
    return parsed


def load_functions(entries: list, type_names: set) -> list:
    parsed = []
    indices = set()
    names = set()
    for entry in entries:
        if not isinstance(entry, dict) or not set(entry) <= {"index", "name", "parameters"}:
            raise ValueError("functions entries must be objects with index, name and parameters")
        index = entry.get("index")
        name = entry.get("name")
        parameters = entry.get("parameters")
        if not isinstance(index, int) or isinstance(index, bool) or not 0 <= index <= MAX_INDEX:
            raise ValueError(f"function index must be 0..65535: {index!r}")
        if index in indices:
            raise ValueError(f"duplicate function index: {index}")
        indices.add(index)
        if not isinstance(name, str) or not FUNCTION_NAME.match(name):
            raise ValueError(f"function name is invalid: {name!r}")
        if name.lower() in names:
            raise ValueError(f"duplicate function name: {name}")
        names.add(name.lower())
        if not isinstance(parameters, list) or len(parameters) > MAX_PARAMETERS:
            raise ValueError(f"function {name} needs 0 to 3 parameters")
        for parameter in parameters:
            if not isinstance(parameter, str) or parameter not in type_names:
                raise ValueError(f"function {name} has unknown parameter type: {parameter!r}")
        parsed.append({"index": index, "name": name, "parameters": list(parameters)})
    parsed.sort(key=lambda row: row["index"])
    return parsed


def load_run_on(entries: list, type_names: set) -> list:
    parsed = []
    values = set()
    names = set()
    for entry in entries:
        if not isinstance(entry, dict) or set(entry) != {"value", "name", "parameter3", "reference_form_id"}:
            raise ValueError(
                "run_on entries must be objects with value, name, parameter3 and reference_form_id"
            )
        value = entry.get("value")
        name = entry.get("name")
        parameter3 = entry.get("parameter3")
        reference = entry.get("reference_form_id")
        if not isinstance(value, int) or isinstance(value, bool) or not 0 <= value <= MAX_RUN_ON_VALUE:
            raise ValueError(f"Run On value must be 0..{MAX_RUN_ON_VALUE}: {value!r}")
        if value in values:
            raise ValueError(f"duplicate Run On value: {value}")
        values.add(value)
        if not isinstance(name, str) or not RUN_ON_NAME.match(name):
            raise ValueError(f"Run On name is invalid: {name!r}")
        if name.lower() in names:
            raise ValueError(f"duplicate Run On name: {name}")
        names.add(name.lower())
        if parameter3 not in type_names:
            raise ValueError(f"Run On {name} has an unknown parameter type for parameter 3: {parameter3!r}")
        if not isinstance(reference, bool):
            raise ValueError(f"Run On {name} reference_form_id must be a boolean: {reference!r}")
        if parsed and value <= parsed[-1]["value"]:
            raise ValueError("Run On values must be ascending")
        parsed.append(
            {
                "value": value,
                "name": name,
                "parameter3": parameter3,
                "reference_form_id": reference,
            }
        )
    return parsed


def load_table(schema_path: Path):
    document = json.loads(schema_path.read_text(encoding="utf-8"))
    if document.get("version") != 1:
        raise ValueError("unsupported Fallout 4 condition function schema version")
    types = document.get("parameter_types")
    functions = document.get("functions")
    run_on = document.get("run_on")
    if not isinstance(types, list) or not types:
        raise ValueError("parameter_types must be a non-empty array")
    if not isinstance(functions, list) or not functions:
        raise ValueError("functions must be a non-empty array")
    if not isinstance(run_on, list) or not run_on:
        raise ValueError("run_on must be a non-empty array")
    parsed_types = load_parameter_types(types)
    type_names = {row["name"] for row in parsed_types}
    parsed_functions = load_functions(functions, type_names)
    parsed_run_on = load_run_on(run_on, type_names)
    return parsed_types, parsed_functions, parsed_run_on


def render(types, functions, run_on) -> str:
    lines = [
        "// Generated by Radicle/tools/condition_function_codegen.py from schema/fallout4/condition_functions.json.",
        "// Do not hand-edit; update the JSON source and regenerate.",
    ]
    for row in types:
        if row["kind"] == "form_link":
            signatures = ", ".join(f"Signature::from_string({cpp_string(target)})" for target in row["targets"])
            lines.append(f"inline constexpr Signature k_condition_targets_{row['name']}[] = {{{signatures}}};")
    lines.append("inline constexpr ConditionParameterType k_generated_condition_parameter_types[] = {")
    position = {}
    for slot, row in enumerate(types):
        position[row["name"]] = slot
        targets = f"k_condition_targets_{row['name']}" if row["kind"] == "form_link" else "{}"
        lines.append(
            "  {" +
            f"{cpp_string(row['name'])}, ConditionParameterKind::{KINDS[row['kind']]}, {targets}" +
            "},"
        )
    lines.append("};")
    lines.append("inline constexpr ConditionFunction k_generated_condition_functions[] = {")
    for row in functions:
        slots = [f"&k_generated_condition_parameter_types[{position[name]}]" for name in row["parameters"]]
        slots += ["nullptr"] * (MAX_PARAMETERS - len(slots))
        lines.append(
            "  {" +
            f"{row['index']}, {cpp_string(row['name'])}, {{{', '.join(slots)}}}" +
            "},"
        )
    lines.append("};")
    lines.append("inline constexpr ConditionRunOn k_generated_condition_run_on[] = {")
    for row in run_on:
        lines.append(
            "  {" +
            f"{row['value']}, {cpp_string(row['name'])}, "
            f"&k_generated_condition_parameter_types[{position[row['parameter3']]}], "
            f"{'true' if row['reference_form_id'] else 'false'}" +
            "},"
        )
    lines.append("};")
    return "\n".join(lines) + "\n"


def render_ctda(types, functions) -> str:
    kinds = {row["name"]: row["kind"] for row in types}
    rows = []
    for row in functions:
        flags = 0
        for slot, name in enumerate(row["parameters"][:2]):
            if kinds[name] == "form_link":
                flags |= 1 << slot
        if flags:
            rows.append(f"{{{row['index']}, {flags}}},  // {row['name']}")
    lines = [
        "// Generated by Radicle/tools/condition_function_codegen.py from schema/fallout4/condition_functions.json.",
        "// Do not hand-edit; update the JSON source and regenerate.",
        "// Each row is {function_index, flags}: bit0 = param1 is a form_link, bit1 = param2 is a form_link.",
        f"// {len(rows)} functions.",
    ]
    return "\n".join(lines + rows) + "\n"


def write_or_check(path: Path, expected: str, check: bool, label: str) -> bool:
    if check:
        if not path.exists() or path.read_text(encoding="utf-8") != expected:
            print(f"{label} is stale: {path}")
            return False
        return True
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(expected, encoding="utf-8")
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--schema", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--ctda-output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    types, functions, run_on = load_table(args.schema)
    writes = [
        (args.output, render(types, functions, run_on), "generated condition function table"),
        (args.ctda_output, render_ctda(types, functions), "generated CTDA function flags"),
    ]
    ok = True
    for path, expected, label in writes:
        ok = write_or_check(path, expected, args.check, label) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
