#!/usr/bin/env python3

import argparse
import hashlib
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

CP_PRIORITY = {
    "cpIgnore": "ignore",
    "cpBenignIfAdded": "benign_if_added",
    "cpBenign": "benign",
    "cpCritical": "critical",
}
CP_OTHER = frozenset({"cpNormal", "cpNormalIgnoreEmpty", "cpOverride", "cpTranslate"})
CPP_PRIORITY = {
    "ignore": "ConflictPriority::ignore",
    "benign_if_added": "ConflictPriority::benign_if_added",
    "benign": "ConflictPriority::benign",
    "critical": "ConflictPriority::critical",
}

SOURCE_FILES = ("wbDefinitionsSignatures.pas", "wbDefinitionsCommon.pas", "wbDefinitionsFO4.pas")
ROUTINE_FILES = ("wbDefinitionsCommon.pas", "wbDefinitionsFO4.pas")
DEFAULT_FIELDS = Path(__file__).resolve().parents[1] / "schema" / "fallout4" / "fields.json"
SIGNATURE_VALUE = re.compile(r"^[A-Z][A-Z0-9_]{3}$")
CONST_DEFINITION = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*(?::\s*\w+\s*)?=\s*'([^']*)'\s*;")

TOKEN_RE = re.compile(
    r"""
    (?P<ws>\s+)
  | (?P<line>//[^\n]*)
  | (?P<block>\{[^}]*\}|\(\*.*?\*\))
  | (?P<str>'(?:[^']|'')*')
  | (?P<char>\#\$[0-9A-Fa-f]+|\#\d+)
  | (?P<num>\$[0-9A-Fa-f]+|\d+(?:\.\d+)?)
  | (?P<ident>[A-Za-z_][A-Za-z0-9_]*)
  | (?P<op>:=|<>|<=|>=|\.\.|[()\[\],;:.=<>+\-*/^@])
    """,
    re.VERBOSE | re.DOTALL,
)

BLOCK_OPENERS = frozenset({"begin", "try", "case"})
ROUTINE_DIRECTIVES = frozenset(
    {"override", "overload", "final", "virtual", "reintroduce", "inline", "stdcall", "cdecl",
     "register", "forward", "external", "dynamic", "abstract", "static"}
)
BINARY_OPERATORS = frozenset(
    {"+", "-", "*", "/", "=", "<>", "<", ">", "<=", ">=", "in", "and", "or", "xor", "shl", "shr",
     "div", "mod", "as", "is"}
)
REGISTER_CALLS = frozenset({"registerrecorddef", "registerrefrecorddef"})
MAX_CALL_DEPTH = 96
MAX_BIND_DEPTH = 6
HAND_RULE_EXCLUSIONS = {
    ("PACK", "CITC"): "hand scope keeps CITC non-benign outside documented roots (test-pinned)",
    ("SCEN", "CITC"): "hand scope keeps CITC non-benign outside documented roots (test-pinned)",
    ("QUST", "COCT"): "scoped KSIZ/COCT alias path owns QUST COCT (conflict_kind.cpp scoped, test-pinned)",
}


class ParseError(Exception):
    pass


@dataclass
class Token:
    kind: str
    text: str
    line: int

    @property
    def lower(self) -> str:
        return self.text.lower()


def tokenize(text: str, name: str) -> list:
    tokens = []
    pos = 0
    line = 1
    while pos < len(text):
        match = TOKEN_RE.match(text, pos)
        if match is None:
            raise ParseError(f"{name}:{line}: unexpected character {text[pos]!r}")
        kind = match.lastgroup
        chunk = match.group()
        if kind not in ("ws", "line", "block"):
            tokens.append(Token(kind, chunk, line))
        line += chunk.count("\n")
        pos = match.end()
    return tokens


@dataclass
class Node:
    kind: str
    text: str = ""
    args: list = field(default_factory=list)
    base: object = None
    items: list = field(default_factory=list)
    line: int = 0
    file: str = ""


@dataclass
class Scalar:
    text: str
    value: object = None
    is_string: bool = False
    is_signature: bool = False


@dataclass
class ListVal:
    items: list


@dataclass
class Group:
    items: list


@dataclass
class Elem:
    call: str
    sig: object
    name: object
    cp: object
    children: list
    origin: str
    dynamic: bool = False


@dataclass
class Routine:
    name: str
    file: str
    params: list
    start: int
    end: int
    assigns: dict
    results: list
    registrations: list = field(default_factory=list)


class Env:
    def __init__(self, file: str, routine=None):
        self.file = file
        self.routine = routine
        self.params = {}


class Parser:
    def __init__(self, tokens: list, file: str):
        self.tokens = tokens
        self.file = file

    def tok(self, i: int) -> Token:
        if i < len(self.tokens):
            return self.tokens[i]
        last = self.tokens[-1].line if self.tokens else 0
        return Token("eof", "", last)

    def fail(self, i: int, message: str):
        token = self.tok(i)
        raise ParseError(f"{self.file}:{token.line}: {message} near {token.text!r}")

    def expect(self, i: int, text: str) -> int:
        if self.tok(i).text != text:
            self.fail(i, f"expected {text!r}")
        return i + 1

    def block_end(self, i: int) -> int:
        depth = 0
        while i < len(self.tokens):
            token = self.tokens[i]
            if token.kind == "ident" and token.lower in BLOCK_OPENERS:
                depth += 1
            elif token.kind == "ident" and token.lower == "end":
                depth -= 1
                if depth == 0:
                    return i
            i += 1
        self.fail(i, "unterminated block")

    def parse_expr(self, i: int):
        left, i = self.parse_unary(i)
        while True:
            token = self.tok(i)
            if token.kind in ("op", "ident") and token.lower in BINARY_OPERATORS:
                right, i = self.parse_unary(i + 1)
                left = Node("bin", text=token.lower, args=[left, right], line=token.line,
                            file=self.file)
                continue
            return left, i

    def parse_unary(self, i: int):
        token = self.tok(i)
        if token.lower in ("not", "@") or token.text in ("-", "+"):
            operand, i = self.parse_unary(i + 1)
            return Node("op", args=[operand], line=token.line, file=self.file), i
        return self.parse_postfix(i)

    def parse_postfix(self, i: int):
        node, i = self.parse_primary(i)
        while True:
            token = self.tok(i)
            if token.text == "." and self.tok(i + 1).kind == "ident":
                member = self.tok(i + 1)
                i += 2
                if node.kind == "ident" and self.tok(i).text != "(":
                    node = Node("ident", text=f"{node.text}.{member.text}", line=member.line,
                                file=self.file)
                    continue
                args = []
                if self.tok(i).text == "(":
                    args, i = self.parse_args(i)
                node = Node("post", text=member.lower, base=node, args=args, line=member.line,
                            file=self.file)
                continue
            if token.text == "(" and node.kind == "ident":
                args, i = self.parse_args(i)
                node = Node("call", text=node.text, args=args, line=token.line, file=self.file)
                continue
            if token.text == "^":
                node = Node("deref", base=node, line=token.line, file=self.file)
                i += 1
                continue
            if token.text == "[":
                indexes, i = self.parse_list(i + 1, "]")
                node = Node("index", base=node, args=indexes, line=token.line, file=self.file)
                continue
            return node, i

    def parse_list(self, i: int, closer: str):
        items = []
        if self.tok(i).text == closer:
            return items, i + 1
        while True:
            item, i = self.parse_expr(i)
            items.append(item)
            if self.tok(i).text == ",":
                i += 1
                continue
            return items, self.expect(i, closer)

    def parse_args(self, i: int):
        i = self.expect(i, "(")
        return self.parse_list(i, ")")

    def parse_primary(self, i: int):
        token = self.tok(i)
        if token.kind == "str":
            text = token.text[1:-1].replace("''", "'")
            return Node("str", text=text, line=token.line, file=self.file), i + 1
        if token.kind in ("char", "num"):
            return Node("num", text=token.text, line=token.line, file=self.file), i + 1
        if token.kind == "ident":
            if token.lower == "nil":
                return Node("nil", line=token.line, file=self.file), i + 1
            if token.lower in ("procedure", "function"):
                return Node("opaque", line=token.line, file=self.file), self.skip_anonymous(i)
            if self.tok(i + 1).text == "<" and self.tok(i + 2).kind == "ident" and \
                    self.tok(i + 3).text == ">":
                return Node("ident", text=token.text, line=token.line, file=self.file), i + 4
            return Node("ident", text=token.text, line=token.line, file=self.file), i + 1
        if token.text == "(":
            node, i = self.parse_expr(i + 1)
            return node, self.expect(i, ")")
        if token.text == "[":
            items = []
            i += 1
            if self.tok(i).text == "]":
                return Node("arr", items=items, line=token.line, file=self.file), i + 1
            while True:
                item, i = self.parse_expr(i)
                items.append(item)
                if self.tok(i).text == ",":
                    i += 1
                    continue
                i = self.expect(i, "]")
                return Node("arr", items=items, line=token.line, file=self.file), i
        self.fail(i, "unsupported expression")

    def skip_anonymous(self, i: int) -> int:
        depth = 0
        start = i
        while i < len(self.tokens):
            token = self.tokens[i]
            if token.text == "(":
                depth += 1
            elif token.text == ")":
                depth -= 1
            elif depth == 0 and token.kind == "ident" and token.lower == "begin":
                return self.block_end(i) + 1
            i += 1
        self.fail(start, "unterminated anonymous routine")


def parse_params(parser: Parser, i: int):
    params = []
    if parser.tok(i).text != "(":
        return params, i
    i += 1
    depth = 0
    segment_start = i
    segments = []
    while True:
        token = parser.tok(i)
        if token.kind == "eof":
            parser.fail(i, "unterminated parameter list")
        if token.text in ("(", "["):
            depth += 1
        elif token.text in (")", "]"):
            if depth == 0:
                segments.append((segment_start, i))
                i += 1
                break
            depth -= 1
        elif token.text == ";" and depth == 0:
            segments.append((segment_start, i))
            segment_start = i + 1
        i += 1
    for begin, end in segments:
        j = begin
        while j < end and parser.tokens[j].lower in ("const", "var", "out"):
            j += 1
        names = []
        while j < end and parser.tokens[j].kind == "ident":
            names.append(parser.tokens[j].text)
            j += 1
            if j < end and parser.tokens[j].text == ",":
                j += 1
                continue
            break
        default = None
        k = j
        while k < end and parser.tokens[k].text != "=":
            k += 1
        if k < end:
            default, _ = parser.parse_expr(k + 1)
        for name in names:
            params.append((name, default))
    return params, i


def parse_routine(parser: Parser, i: int):
    tokens = parser.tokens
    j = i + 1
    name = tokens[j].text
    j += 1
    while parser.tok(j).text == "." and parser.tok(j + 1).kind == "ident":
        name = parser.tok(j + 1).text
        j += 2
    params, j = parse_params(parser, j)
    while parser.tok(j).text != ";":
        if parser.tok(j).kind == "eof" or parser.tok(j).lower in ("begin", "end"):
            return None, j
        j += 1
    j += 1
    while parser.tok(j).kind == "ident" and parser.tok(j).lower in ROUTINE_DIRECTIVES:
        j += 1
        if parser.tok(j).text == ";":
            j += 1
    while j < len(tokens):
        token = tokens[j]
        if token.kind == "ident" and token.lower == "begin":
            end = parser.block_end(j)
            routine = Routine(name=name, file=parser.file, params=params, start=j, end=end,
                              assigns={}, results=[])
            scan_body(parser, routine)
            return routine, end + 1
        if token.kind == "ident" and token.lower in ("function", "procedure") and \
                parser.tok(j + 1).kind == "ident":
            _, j = parse_routine(parser, j)
            continue
        if token.kind == "ident" and token.lower in ("end", "implementation", "interface",
                                                       "initialization", "finalization"):
            return None, j
        j += 1
    return None, j


def parse_routines(parser: Parser, start: int) -> list:
    routines = []
    i = start
    while i < len(parser.tokens):
        token = parser.tokens[i]
        if token.kind == "ident" and token.lower in ("function", "procedure") and \
                parser.tok(i + 1).kind == "ident":
            routine, i = parse_routine(parser, i)
            if routine is not None:
                routines.append(routine)
            continue
        i += 1
    return routines


def scan_body(parser: Parser, routine: Routine) -> None:
    tokens = parser.tokens
    j = routine.start + 1
    while j < routine.end:
        token = tokens[j]
        if token.text == ":=":
            target_index = j - 1
            if tokens[target_index].text == "]":
                depth = 0
                k = target_index
                while k >= 0:
                    if tokens[k].text == "]":
                        depth += 1
                    elif tokens[k].text == "[":
                        depth -= 1
                        if depth == 0:
                            break
                    k -= 1
                target_index = k - 1
            target = tokens[target_index]
            if target.kind == "ident":
                node, j = parser.parse_expr(j + 1)
                if target.lower == "result":
                    routine.results.append(node)
                else:
                    routine.assigns.setdefault(target.text, []).append(node)
                continue
        elif token.kind == "ident" and token.lower == "exit" and parser.tok(j + 1).text == "(":
            node, j = parser.parse_expr(j + 1)
            routine.results.append(node)
            continue
        j += 1


@dataclass
class RegisterSite:
    file: str
    line: int
    call: Node
    routine: object


def find_registrations(parser: Parser, routines: list, start: int) -> list:
    sites = []
    tokens = parser.tokens
    for i in range(start, len(tokens)):
        token = tokens[i]
        if token.kind == "ident" and token.lower in REGISTER_CALLS and parser.tok(i + 1).text == "(":
            call, _ = parser.parse_expr(i)
            while call.kind == "post":
                call = call.base
            enclosing = enclosing_routine(routines, parser.file, i)
            sites.append(RegisterSite(parser.file, token.line, call, enclosing))
    return sites


def enclosing_routine(routines: list, file: str, index: int):
    return next((routine for routine in routines
                 if routine.file == file and routine.start <= index <= routine.end), None)


def is_declaration(parser: Parser, i: int) -> bool:
    j = i - 1
    while parser.tok(j).text == "." and parser.tok(j - 1).kind == "ident":
        j -= 2
    return parser.tok(j).lower in ("function", "procedure")


def index_calls(parsers: list) -> dict:
    index = {}
    for parser in parsers:
        for i, token in enumerate(parser.tokens):
            if token.kind == "ident" and parser.tok(i + 1).text == "(" and \
                    not is_declaration(parser, i):
                index.setdefault(token.lower, []).append((parser, i))
    return index


def routine_instances(routine, routines: list, calls: dict, depth: int) -> list:
    """One bound Env per call site of routine; a parameterless routine needs none."""
    if routine is None:
        return []
    if not routine.params:
        return [Env(routine.file, routine)]
    if depth >= MAX_BIND_DEPTH:
        return []
    instances = []
    for parser, i in calls.get(routine.name.lower(), []):
        if parser.file == routine.file and routine.start <= i <= routine.end:
            continue
        try:
            node, _ = parser.parse_expr(i)
        except ParseError:
            continue
        caller = enclosing_routine(routines, parser.file, i)
        if caller is not None and caller is not routine and caller.params:
            caller_envs = routine_instances(caller, routines, calls, depth + 1)
        else:
            caller_envs = [Env(parser.file, caller)]
        for caller_env in caller_envs:
            env = Env(routine.file, routine)
            for index, (pname, default) in enumerate(routine.params):
                if index < len(node.args):
                    env.params[pname] = (node.args[index], caller_env)
                else:
                    env.params[pname] = (default, env)
            instances.append(env)
    return instances


class Evaluator:
    def __init__(self, routines: list, consts: dict):
        self.by_name = {}
        for routine in routines:
            self.by_name.setdefault(routine.name.lower(), []).append(routine)
        self.consts = consts
        self.file_globals = {}
        self.any_globals = {}
        self.build_globals(routines)
        self.routine_envs = {}
        self.active = set()
        self.depth = 0
        self.unresolved = set()
        self.dynamic_callbacks = 0

    def build_globals(self, routines: list) -> None:
        owners = {}
        for routine in routines:
            for name in routine.assigns:
                owners.setdefault((routine.file, name), []).append(routine)
                owners.setdefault((None, name), []).append(routine)
        for (file, name), candidates in owners.items():
            if len(candidates) != 1:
                continue
            routine = candidates[0]
            if file is None:
                self.any_globals[name] = routine
            else:
                self.file_globals[(file, name)] = routine

    def routine_env(self, routine: Routine) -> Env:
        key = id(routine)
        if key not in self.routine_envs:
            self.routine_envs[key] = Env(routine.file, routine)
        return self.routine_envs[key]

    def pick_routine(self, name: str, file: str):
        candidates = self.by_name.get(name.split(".")[-1].lower())
        if not candidates:
            return None
        for candidate in candidates:
            if candidate.file == file:
                return candidate
        return candidates[0]

    def eval(self, node, env: Env):
        if node is None:
            return Scalar("nil")
        kind = node.kind
        if kind == "str":
            return Scalar(node.text, value=node.text, is_string=True)
        if kind == "num":
            return Scalar(node.text, value=node.text)
        if kind == "nil":
            return Scalar("nil")
        if kind == "arr":
            return ListVal([self.eval(item, env) for item in node.items])
        if kind == "ident":
            return self.eval_ident(node.text, env)
        if kind == "call":
            return self.eval_call(node, env)
        if kind == "post":
            return self.eval_post(node, env)
        if kind == "bin":
            for operand in node.args:
                self.eval(operand, env)
            return Scalar("<expr>")
        if kind == "index":
            self.eval(node.base, env)
            return Scalar("<index>")
        return Scalar("<opaque>")

    def eval_bound(self, name: str, env: Env, owner_env: Env, values: list):
        key = (id(owner_env), name)
        if key in self.active:
            return Scalar(name)
        self.active.add(key)
        try:
            return combine([self.eval(value, owner_env) for value in values])
        finally:
            self.active.discard(key)

    def eval_ident(self, name: str, env: Env):
        if "." not in name:
            if name in env.params:
                node, bound_env = env.params[name]
                return self.eval(node, bound_env) if node is not None else Scalar(name)
            if env.routine is not None and name in env.routine.assigns:
                return self.eval_bound(name, env, env, env.routine.assigns[name])
            owner = self.file_globals.get((env.file, name)) or self.any_globals.get(name)
            if owner is not None:
                return self.eval_bound(name, env, self.routine_env(owner), owner.assigns[name])
        if name in self.consts:
            return Scalar(name, value=self.consts[name], is_signature=True)
        if name in CP_PRIORITY or name in CP_OTHER:
            return Scalar(name)
        candidates = self.by_name.get(name.split(".")[-1].lower())
        if candidates and "." not in name and not candidates[0].params:
            return self.call_routine(self.pick_routine(name, env.file), [], env)
        if name[:2].lower() == "wb" and "." not in name:
            self.unresolved.add(name)
        return Scalar(name)

    def eval_call(self, node, env: Env):
        bare = node.text.split(".")[-1].lower()
        routine = self.pick_routine(node.text, env.file)
        if routine is not None:
            return self.call_routine(routine, node.args, env)
        if bare == "iftthen":
            return Group([self.eval(arg, env) for arg in node.args[1:]])
        args = [self.eval(arg, env) for arg in node.args]
        if bare.startswith("wb"):
            return make_element(node, args)
        elements = []
        for value in args:
            elements.extend(flatten_elements(value))
        return Group(elements) if elements else Scalar("<call>")

    def call_routine(self, routine, args: list, caller: Env):
        if routine is None:
            return Scalar("<missing>")
        if self.depth > MAX_CALL_DEPTH:
            raise ParseError(f"{routine.file}: call depth exceeded at {routine.name}")
        callee = Env(routine.file, routine)
        for index, (pname, default) in enumerate(routine.params):
            if index < len(args):
                callee.params[pname] = (args[index], caller)
            elif default is not None:
                callee.params[pname] = (default, callee)
            else:
                callee.params[pname] = (None, callee)
        self.depth += 1
        try:
            return combine([self.eval(result, callee) for result in routine.results])
        finally:
            self.depth -= 1

    def eval_post(self, node, env: Env):
        base = self.eval(node.base, env)
        if node.text == "setgetcp":
            self.dynamic_callbacks += 1
            if isinstance(base, Elem):
                return Elem(base.call, base.sig, base.name, base.cp, base.children, base.origin,
                            dynamic=True)
        return base


def combine(values: list):
    if not values:
        return Scalar("<none>")
    if len(values) == 1:
        return values[0]
    return Group(values)


def make_element(node, args: list) -> Elem:
    sig = None
    name = None
    cp = None
    if args and isinstance(args[0], Scalar) and args[0].is_signature:
        sig = args[0].value
    for value in args:
        if isinstance(value, Scalar) and value.is_string and name is None:
            name = value.text
        if isinstance(value, Scalar) and not value.is_string and value.text in CP_PRIORITY:
            cp = value.text
    children = []
    for value in args:
        children.extend(flatten_elements(value))
    return Elem(call=node.text, sig=sig, name=name, cp=cp, children=children,
                origin=f"{node.file}:{node.line}")


def flatten_elements(value) -> list:
    if isinstance(value, Elem):
        return [value]
    if isinstance(value, (Group, ListVal)):
        out = []
        for item in value.items:
            out.extend(flatten_elements(item))
        return out
    return []


def contains_signature(elem: Elem) -> bool:
    if elem.sig is not None:
        return True
    return any(contains_signature(child) for child in elem.children)


def add_mark(marks: list, record: str, kind: str, sub, name, elem: Elem, cp: str,
             path: tuple = ()) -> None:
    marks.append({"record": record, "kind": kind, "subrecord": sub, "field": name,
                  "path": list(path), "cp": cp, "priority": CP_PRIORITY.get(cp),
                  "origin": elem.origin, "dynamic": elem.dynamic})


def walk_members(record: str, members: list, sub, marks: list, prefix: tuple = ()) -> None:
    for member in members:
        if member.sig is not None:
            if member.cp is not None:
                add_mark(marks, record, "subrecord", member.sig, None, member, member.cp)
            walk_members(record, member.children, member.sig, marks)
            continue
        chain = prefix + ((member.name,) if member.name is not None else ())
        if member.cp is not None:
            if sub is None or contains_signature(member):
                add_mark(marks, record, "container", sub, member.name, member, member.cp, prefix)
            else:
                add_mark(marks, record, "field", sub, member.name, member, member.cp, prefix)
        walk_members(record, member.children, sub, marks, chain)


def extract(core_dir: Path) -> dict:
    texts = {}
    hashes = {}
    for name in SOURCE_FILES:
        raw = (core_dir / name).read_bytes()
        hashes[name] = hashlib.sha256(raw).hexdigest()
        texts[name] = raw.decode("cp1252").replace("\r\n", "\n")
    consts = {}
    for text in texts.values():
        for match in CONST_DEFINITION.finditer(text):
            if match.group(1) == match.group(2) and SIGNATURE_VALUE.match(match.group(2)):
                consts[match.group(1)] = match.group(2)
    routines = []
    per_file = []
    for name in ROUTINE_FILES:
        tokens = tokenize(texts[name], name)
        parser = Parser(tokens, name)
        impl = next(index for index, token in enumerate(tokens)
                    if token.kind == "ident" and token.lower == "implementation")
        file_routines = parse_routines(parser, impl)
        routines.extend(file_routines)
        per_file.append((parser, file_routines, impl))
    evaluator = Evaluator(routines, consts)
    calls = index_calls([parser for parser, _, _ in per_file])
    marks = []
    seen = set()
    records = []
    unresolved_registrations = []
    registration_count = 0
    for parser, file_routines, impl in per_file:
        for site in find_registrations(parser, file_routines, impl):
            where = f"{site.file}:{site.line}"
            envs = routine_instances(site.routine, routines, calls, 0) if site.routine else \
                [Env(site.file)]
            if not envs:
                unresolved_registrations.append({"origin": where, "reason": "helper never called"})
                continue
            for env in envs:
                args = [evaluator.eval(arg, env) for arg in site.call.args]
                if not args or not isinstance(args[0], Scalar) or not args[0].is_signature:
                    unresolved_registrations.append({"origin": where,
                                                     "reason": "record signature not constant"})
                    continue
                lists = [value for value in args if isinstance(value, ListVal)]
                if not lists:
                    unresolved_registrations.append({"origin": where,
                                                     "reason": "no member list"})
                    continue
                record = args[0].value
                records.append(record)
                registration_count += 1
                site_marks = []
                for value in args:
                    if isinstance(value, Scalar) and value.text in CP_PRIORITY:
                        origin = Elem("register", None, None, value.text, [], where)
                        add_mark(site_marks, record, "record", None, None, origin, value.text)
                walk_members(record, flatten_elements(lists[-1]), None, site_marks)
                for mark in site_marks:
                    key = json.dumps(mark, sort_keys=True)
                    if key not in seen:
                        seen.add(key)
                        marks.append(mark)
    return {
        "version": 1,
        "sources": hashes,
        "records": sorted(set(records)),
        "registrations": registration_count,
        "routines": len(routines),
        "dynamic_callbacks": evaluator.dynamic_callbacks,
        "unresolved": sorted(evaluator.unresolved),
        "unresolved_registrations": unresolved_registrations,
        "marks": sorted(marks, key=lambda row: (row["origin"], row["record"], row["kind"],
                                               str(row["subrecord"]), str(row["field"]),
                                               str(row["cp"]))),
    }


def cpp_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def unmapped_row(row: dict, reason: str) -> dict:
    return {"record": row["record"], "subrecord": row["subrecord"] or "",
            "field": row["field"] or "", "kind": row["kind"], "priority": row["priority"] or "",
            "origin": row["origin"], "reason": reason}


def normalize_label(text: str) -> str:
    return re.sub(r"[^a-z0-9]", "", text.lower())


def field_rows_by_subrecord(fields_path: Path) -> dict:
    document = json.loads(fields_path.read_text(encoding="utf-8"))
    rows = {}
    for record in document["records"]:
        for field in record["fields"]:
            if "subrecord" in field:
                rows.setdefault((record["signature"], field["subrecord"]), []).append(field)
    return rows


def map_field(row: dict, rows: dict):
    if row["field"] is None:
        return None, "field name is not a literal"
    candidates = rows.get((row["record"], row["subrecord"]))
    if not candidates:
        return None, "subrecord has no field rows in fields.json"
    parts = list(row["path"]) + [row["field"]]
    for start in range(len(parts)):
        wanted = normalize_label(" ".join(parts[start:]))
        matches = [field for field in candidates if normalize_label(field["label"]) == wanted]
        if len(matches) > 1:
            return None, "ambiguous field label"
        if len(matches) == 1:
            match = matches[0]
            if match["kind"] != "scalar":
                return None, f"matched {match['kind']} row, field classifier reads scalar rows only"
            return match["path"], None
    return None, "no field row label matches"


def field_rules_for(field_marks: list, rows: dict, unmapped: list) -> list:
    grouped = {}
    for row in field_marks:
        key = (row["record"], row["subrecord"])
        if key in HAND_RULE_EXCLUSIONS:
            unmapped.append(unmapped_row(row, HAND_RULE_EXCLUSIONS[key]))
            continue
        path, reason = map_field(row, rows)
        if path is None:
            unmapped.append(unmapped_row(row, reason))
            continue
        grouped.setdefault((row["record"], row["subrecord"], path), []).append(row)
    field_rules = []
    for key in sorted(grouped):
        group = grouped[key]
        if len({row["priority"] for row in group}) != 1:
            for row in group:
                unmapped.append(unmapped_row(row, "conflicting marks for the same field"))
            continue
        field_rules.append({"record": key[0], "subrecord": key[1], "path": key[2],
                            "priority": group[0]["priority"], "origin": group[0]["origin"]})
    return field_rules


def render(extraction: dict, fields_path: Path) -> tuple:
    subrecord_marks = {}
    field_marks = []
    unmapped = []
    for row in extraction["marks"]:
        if row["dynamic"]:
            unmapped.append(unmapped_row(row, "dynamic GetCP callback"))
        elif row["kind"] == "record":
            unmapped.append(unmapped_row(row, "record-level priority has no table slot"))
        elif row["kind"] == "subrecord":
            subrecord_marks.setdefault((row["record"], row["subrecord"]), []).append(row)
        elif row["kind"] == "field":
            field_marks.append(row)
        else:
            unmapped.append(unmapped_row(row, "container mark over a group, subrecord granularity only"))
    field_rules = field_rules_for(field_marks, field_rows_by_subrecord(fields_path), unmapped)
    rules = []
    for key in sorted(subrecord_marks):
        rows = subrecord_marks[key]
        if key in HAND_RULE_EXCLUSIONS:
            for row in rows:
                unmapped.append(unmapped_row(row, HAND_RULE_EXCLUSIONS[key]))
            continue
        if len({row["priority"] for row in rows}) != 1:
            for row in rows:
                unmapped.append(unmapped_row(row, "conflicting marks for the same subrecord"))
            continue
        rules.append({"record": key[0], "subrecord": key[1], "priority": rows[0]["priority"],
                      "origin": rows[0]["origin"]})
    for row in extraction["unresolved_registrations"]:
        unmapped.append({"record": "", "subrecord": "", "field": "", "kind": "registration",
                         "priority": "", "origin": row["origin"], "reason": row["reason"]})
    unmapped.sort(key=lambda row: (row["origin"], row["record"], row["subrecord"], row["field"]))
    return rules, field_rules, unmapped


def render_inc(rules: list, field_rules: list, sources: dict) -> str:
    digests = ", ".join(f"{name}={digest}" for name, digest in sorted(sources.items()))
    lines = [
        "// Generated by Radicle/tools/xedit_conflict_priority_codegen.py from xEdit Core. Do not hand-edit.",
        f"// xEdit source sha256: {digests}",
        "inline constexpr ConflictPriorityRule k_xedit_subrecord_priorities[] = {",
    ]
    for row in rules:
        lines.append(
            "  {" +
            f"Signature::from_string({cpp_string(row['record'])}), "
            f"Signature::from_string({cpp_string(row['subrecord'])}), "
            f"{CPP_PRIORITY[row['priority']]}" +
            "},"
        )
    lines.append("};")
    lines.append("inline constexpr ConflictFieldPriorityRule k_xedit_field_priorities[] = {")
    for row in field_rules:
        lines.append(
            "  {" +
            f"Signature::from_string({cpp_string(row['record'])}), "
            f"Signature::from_string({cpp_string(row['subrecord'])}), "
            f"{cpp_string(row['path'])}, "
            f"{CPP_PRIORITY[row['priority']]}" +
            "},"
        )
    lines.append("};")
    return "\n".join(lines) + "\n"


def write_or_check(path: Path, expected: str, check: bool, label: str) -> bool:
    if check:
        if not path.exists() or path.read_text(encoding="utf-8") != expected:
            print(f"{label} is stale: {path}", file=sys.stderr)
            return False
        return True
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(expected, encoding="utf-8", newline="\n")
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--xedit-core", type=Path)
    parser.add_argument("--marks", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--unmapped", type=Path, required=True)
    parser.add_argument("--fields", type=Path, default=DEFAULT_FIELDS)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    if args.xedit_core is not None:
        extraction = extract(args.xedit_core)
        document = json.dumps(extraction, indent=2, sort_keys=True) + "\n"
        if not write_or_check(args.marks, document, args.check, "xEdit mark extraction"):
            return 1
    extraction = json.loads(args.marks.read_text(encoding="utf-8"))
    rules, field_rules, unmapped = render(extraction, args.fields)
    if not write_or_check(args.output, render_inc(rules, field_rules, extraction["sources"]),
                          args.check, "generated conflict priority table"):
        return 1
    summary = counts(rules, field_rules, unmapped)
    report = json.dumps({"counts": summary, "unmapped": unmapped}, indent=2,
                        sort_keys=True) + "\n"
    if not write_or_check(args.unmapped, report, args.check, "unmapped conflict marks"):
        return 1
    print(json.dumps(summary, sort_keys=True))
    return 0


def counts(rules: list, field_rules: list, unmapped: list) -> dict:
    by_priority = {}
    for rule in rules:
        by_priority[rule["priority"]] = by_priority.get(rule["priority"], 0) + 1
    field_by_priority = {}
    for rule in field_rules:
        field_by_priority[rule["priority"]] = field_by_priority.get(rule["priority"], 0) + 1
    return {"subrecord_rules": len(rules), "by_priority": dict(sorted(by_priority.items())),
            "field_rules": len(field_rules),
            "field_by_priority": dict(sorted(field_by_priority.items())),
            "unmapped": len(unmapped)}


if __name__ == "__main__":
    sys.exit(main())
