#!/usr/bin/env python3
"""validate_ckini.py -- compatibility oracle for CreationKit.ini files.

Replays the Creation Kit's INISettingCollection reader against the embedded
descriptor table and type-letter rules, and reports, for every key the file
contains (or the table declares), exactly what the engine would do:
read-and-apply, read-as-zero, keep-default, or silently ignore.

The oracle implements the reader `FUN_1425c7f90` (RVA 0x25c7f90) and the type
tag `FUN_1425c7210` (RVA 0x25c7210), decompiled from CreationKit.exe
1.11.137.0 -- see CKRE docs 10 (reader), 16 (write-back) and 17 (ini census).

Usage:
    python3 validate_ckini.py <CreationKit.ini> [--descriptors <keys.txt>]
        [--sLanguage <value>] [--strict]
    python3 validate_ckini.py --list-keys    # reference card of all keys
    python3 validate_ckini.py --stats        # type/section distribution
    python3 validate_ckini.py --command '-CompileTextExport:Fallout4.esm en Export Fallout4'
    python3 validate_ckini.py --command=-Install:  # no-argument switches
    python3 validate_ckini.py CreationKit.ini --exit-codes

The descriptor table maps `bKey:Section` literals (1,629 in the shipped dump;
doc 10 counts 1,641 records with static pointers). The type letter is the
FIRST character of the key name:

    b bool | c int8 | h int16 | i int32 | u uint32 | f float |
    s/S string | r RGB "u,u,u" | a RGBA "u,u,u,u" | (other) -> read as uint

Engine contract implemented (from the decompiled reader):

  * section/key split: key = text before the first ':', section = text after
    it (or the literal "MAIN" when the descriptor has no colon)
  * Win32 profile matching is CASE-INSENSITIVE for both section and key
  * b/c/h/i/u are read with GetPrivateProfileIntA: optional whitespace, sign,
    decimal digits; garbage (or empty) reads as 0; never fails
  * f  is read as a string then sscanf("%f"): EMPTY or malformed value -> the
    read fails, the embedded default is retained, and the value-change notify
    does NOT fire
  * s/S reads the raw string (empty is fine) and never fires the notify
  * r/a read a string then sscanf("%u,%u,%u" / "%u,%u,%u,%u"): EMPTY -> read
    fails; partial conversion still OVERWRITES the record with a pack of the
    converted bytes (garbage) and still fires the notify (engine quirk)
  * a descriptor whose section is "LANGUAGE" reads the sLanguage VALUE from
    [General] and uses THAT as the section name (sLanguage indirection)
  * unknown letters fall into the switch's default arm -> read as uint32

Verdicts per key:

  READ           engine applies the value exactly as written
  READ-AS-ZERO   int key with a non-numeric value -> engine reads 0
  STR-READ       string key (any value, incl. empty)
  READ-FAIL      f/r/a key empty or malformed -> engine keeps the default
  UNKNOWN        (section,key) pair has no descriptor -> dead config, the
                 engine never reads it. A known key under the wrong section
                 shows up here with a note naming its registered section(s)
  MISSING        descriptor key absent from the file -> embedded default used
  TYPE9-AS-UINT  first letter not a type letter -> read as uint32 (default arm)

Exit status: 0 = PASS, 1 = at least one READ-FAIL, 2 = usage error.
UNKNOWN/READ-AS-ZERO/MISSING are warnings that only fail under --strict.

The --command mode replays the documented 31-switch batch grammar from docs
05/13, including the one-switch rule, the raw single-ESM ExportDialogue branch,
strtok_s-on-space branches, the quoted
FUN_140490960 helper used by CompileTextExport/GenerateSingleLip, and the
platform/MapInfo/PreVis enum checks.
"""

import argparse
import os
import re
import sys

DEFAULT_DESCRIPTORS = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "ghidra-dump", "ck-settings-keys.txt")

# The type-letter switch from FUN_1425c7210 (verified decompile).
# letter -> (type code, short name)
TYPE_LETTERS = {
    "b": (0, "bool"),
    "c": (1, "int8"),
    "h": (2, "int16"),
    "i": (3, "int32"),
    "u": (4, "uint32"),
    "f": (5, "float"),
    "s": (6, "string"),
    "S": (6, "string"),
    "r": (7, "RGB"),
    "a": (8, "RGBA"),
}
DEFAULT_SLANGUAGE = "ENGLISH"   # PTR_DAT_143b992d0 (not recovered; usual value)


# ---------------------------------------------------------------------------
# Descriptor table
# ---------------------------------------------------------------------------

def parse_descriptors(path):
    """Parse the `bKey:Section` literal list into a lookup table.

    Returns (by_key, by_lower) where:
      by_key   = {(section, key): descriptor-dict}   (canonical case)
      by_lower = {(section_lower, key_lower): descriptor-dict}
    """
    descs = []
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if ":" in line:
                key, section = line.split(":", 1)
            else:
                key, section = line, "MAIN"
            descs.append({"key": key, "section": section,
                          "letter": key[:1] if key else "",
                          "descriptor": line})
    by_key = {(d["section"], d["key"]): d for d in descs}
    by_lower = {(d["section"].lower(), d["key"].lower()): d for d in descs}
    return by_key, by_lower, descs


def type_code(letter):
    """The FUN_1425c7210 switch: first char -> type code (0..9)."""
    if not letter:
        return 9
    return TYPE_LETTERS.get(letter, (9, None))[0]


def type_name(letter):
    if not letter:
        return "none"
    t = TYPE_LETTERS.get(letter)
    return t[1] if t else "unknown"


# ---------------------------------------------------------------------------
# Win32 profile-value parsers (exact semantics of the reader's arms)
# ---------------------------------------------------------------------------

_INT_RE = re.compile(r"^[ \t]*([+-]?)([0-9]+)")


def parse_int_win(value):
    """GetPrivateProfileIntA semantics: optional ws/sign + decimal digits.

    No digits (or garbage) -> 0. Stops at the first non-digit. Returns an
    int in the int32 range (Python ints are unbounded; we mask later).
    """
    m = _INT_RE.match(value or "")
    if not m:
        return 0
    sign, digits = m.group(1), m.group(2)
    n = int(digits)
    if sign == "-":
        n = -n
    # Win32 returns a UINT; the reader then truncates to the target width.
    return n & 0xFFFFFFFF


def sscanf_float(value):
    """sscanf(buf, "%f") == 1 semantics (MSVC strtod).

    Accepts optional whitespace, sign, decimal digits, optional fraction and
    exponent, plus INF/INFINITY/NAN (case-insensitive). Trailing junk after a
    valid float is fine -- one conversion happened.
    """
    s = value or ""
    s = s.lstrip(" \t")
    if not s:
        return False
    head = s[:4].lower()
    if head.startswith(("inf", "nan")) or head.startswith("infi"):
        return True
    m = re.match(r"^[+-]?(?:[0-9]*\.[0-9]+|[0-9]+\.?[0-9]*)(?:[eE][+-]?[0-9]+)?", s)
    if not m or not m.group(0):
        return False
    # A lone sign or '.' with no digits is not a conversion.
    body = m.group(0)
    if body in ("+", "-", ".", "+.", "-."):
        return False
    return True


def sscanf_uint_list(value, count):
    """sscanf(buf, "%u,%u,...") == count semantics.

    %u: skip whitespace, optional sign, >=1 decimal digit. Literal commas
    must match exactly (whitespace is NOT skipped before a literal char, so
    "1 ,2" fails). Returns (conversions_done, [values]).
    """
    s = (value or "").lstrip(" \t")
    vals = []
    for i in range(count):
        if i > 0:
            if not s.startswith(","):
                return i, vals
            s = s[1:]
        s = s.lstrip(" \t")
        m = re.match(r"^([+-]?)([0-9]+)", s)
        if not m:
            return i, vals
        n = int(m.group(2))
        if m.group(1) == "-":
            n = -n
        vals.append(n & 0xFFFFFFFF)
        s = s[m.end():]
    return count, vals


def pack_bgra(r, g, b, a=0xFF):
    """The reader's packing: ((g | r<<8) <<8 | b) <<8 | alpha."""
    return ((g | (r << 8)) << 8 | b) << 8 | a


# ---------------------------------------------------------------------------
# INI parsing (Win32 GetPrivateProfileStringA approximation)
# ---------------------------------------------------------------------------

def parse_ini(text):
    """Parse ini text into {section: {key: value}} preserving case.

    Blank lines and lines starting with ';' are ignored. A line is a key
    only while a section is open (keys before any [section] are dropped).
    Value = everything after the FIRST '=' (trailing whitespace kept).
    """
    sections = {}
    cur = None
    for raw in text.splitlines():
        line = raw.rstrip("\r")
        s = line.strip()
        if not s or s.startswith(";"):
            continue
        if s.startswith("[") and s.endswith("]"):
            cur = s[1:-1].strip()
            sections.setdefault(cur, {})
            continue
        if cur is None:
            continue
        if "=" in line:
            k, v = line.split("=", 1)
            key = k.rstrip()          # leading spaces before key are skipped
            sections[cur][key] = v    # value kept verbatim after '='
    return sections


# ---------------------------------------------------------------------------
# Batch command-line grammar (docs 05-BATCH-CLI.md / 13-CLI-PARSER.md)
# ---------------------------------------------------------------------------

# These are deliberately data, rather than a second parser table hidden in
# the validator. The order is the CK's .data 0x3b8c740 order and therefore also
# supplies the bit number used by the dispatcher.
_BATCH_SPECS = [
    ("-OptimizeMasterFile", 1, 1, None),
    # Execute passes the complete raw slot to the ESM file lookup; unlike
    # CompileTextExport/GenerateLips this branch has no language/path args.
    ("-ExportDialogue", 1, 1, None),
    ("-GenerateLips", 3, 3, None),
    ("-ExportFaceGenData", 2, 2, "platform"),
    ("-TagifyMasterfile", 1, 1, None),
    ("-ExportText", 1, 1, None),
    ("-CompileTextExport", 3, 4, "compile"),
    ("-DelocalizeMasterfile", 1, 1, None),
    ("-SaveDefaultPlugin", 2, 2, None),
    ("-CheckInPlugin", 2, 2, None),
    ("-Install", 0, 0, None),
    ("-DumpNeededFiles", 2, 2, None),
    ("-OptimizeDLC", 1, 1, None),
    ("-GenerateSEQ", 1, 1, None),
    ("-GenerateWarnings", 1, 1, None),
    ("-GenerateAnimInfo", 3, 6, "animinfo"),
    ("-GenerateSingleLip", 2, 2, "quoted"),
    ("-GenerateStaticCollections", 2, 2, "platform"),
    ("-GeneratePreCombined", 1, 5, "previs"),
    ("-GeneratePreVisData", 1, 5, "previs"),
    ("-DepersistRefs", 1, 1, None),
    ("-MapMaker", 2, 2, "worldspace"),
    ("-MapInfo", 3, 3, "mapinfo"),
    ("-OutputAreaArt", 3, 3, None),
    ("-UpdateModelData", 1, 1, None),
    ("-ExportDismemberData", 2, 2, "platform"),
    ("-ConvertMaterials", 0, 0, None),
    ("-ImportScalingData", 1, 1, None),
    ("-CompressPSG", 1, 1, None),
    ("-BuildCDX", 1, 1, None),
    ("-ConvertToESL", 1, 1, None),
]
_BATCH_BY_LOWER = {name.lower(): (idx, name, lo, hi, rule)
                   for idx, (name, lo, hi, rule) in enumerate(_BATCH_SPECS)}
_PLATFORM_TOKENS = {"xb1", "x64", "ps4", "w32"}
_PREVIS_CLEAN = {"clean", "filtered"}
_PREVIS_SCOPE = {"all", "main", "ints", "other"}
_WORLDSPACE_KIND = {"worldspace", "interior"}
_MAPINFO_KIND = {"doors", "markers", "interiors"}
_ANIMINFO_OPTIONS = {"--speed", "--stance", "--forceall"}

# Doc 19's raise-site matrix. The process result is unknown to a static INI
# oracle, so raising branches report both reachable sentinel codes; the two
# no-raise branches report the normal process-exit path instead.
_EXIT_DIRECT = {6, 26, 28}       # CompileTextExport, ConvertMaterials, CompressPSG
_EXIT_NO_RAISE = {10, 16}        # Install, GenerateSingleLip
_EXIT_PROCESS_NAMES = {
    0: "MasterfileOptimizer", 1: "DialogueExporter", 2: "LipGenerator",
    3: "FaceGenDataExporter", 4: "MasterfileTagifier", 5: "MasterfileTextExporter",
    6: "CompileTextExport", 7: "MasterfileDelocalizer", 8: "FileSaver",
    9: "CheckItInChappy", 10: "Install", 11: "NeededFileDumper",
    12: "DLCMasterfileOptimizer", 13: "SEQExporter", 14: "WarningGenerator",
    15: "AnimationBuildProcessDataExport", 16: "GenerateSingleLip",
    17: "StaticCollectionProcess", 18: "PreVisPreCombineDataProcess",
    19: "PreVisPreCombineDataProcess", 20: "DepersistRefsProcess",
    21: "MapMakerProcess", 22: "MapInfoProcess", 23: "AreaArtOutput",
    24: "ModelDataUpdater", 25: "DismemberDataExporter", 26: "MaterialConverter",
    27: "ImportScalingDataProcess", 28: "CompressPSGProcess", 29: "BuildCDX",
    30: "ESLConverter",
}


def _strtok_space(text, limit=None):
    """Return strtok_s(text, " ") tokens and the unconsumed suffix.

    The CK passes the delimiter string containing one literal space. Tabs are
    consequently token characters here, and quotes have no special meaning.
    ``limit`` is used where the native branch consumes only its first N tokens
    before handing the continuation to FUN_140490960.
    """
    tokens = []
    pos = 0
    n = len(text)
    max_tokens = 100 if limit is None else limit
    while len(tokens) < max_tokens:
        while pos < n and text[pos] == " ":
            pos += 1
        if pos >= n:
            return tokens, ""
        start = pos
        while pos < n and text[pos] != " ":
            pos += 1
        tokens.append(text[start:pos])
    return tokens, text[pos:]


def _quoted_helper(text):
    """Mirror FUN_140490960's in-place quoted/unquoted token splitter.

    Returns (token-or-None, remainder). A quoted token may contain spaces;
    quotes are delimiters and are not part of the returned token.
    """
    pos = 0
    n = len(text)
    while pos < n and text[pos].isspace():
        pos += 1
    if pos >= n:
        return None, ""
    if text[pos] == '"':
        pos += 1
        start = pos
        end = text.find('"', pos)
        if end < 0:
            return None, text[start:]
        return text[start:end], text[end + 1:]
    start = pos
    while pos < n and not text[pos].isspace() and text[pos] != '"':
        pos += 1
    return text[start:pos], text[pos:]


def _nonempty_tokens(tokens, label):
    return ["%s must not be empty" % label] if any(t == "" for t in tokens) else []


def _parse_compile_args(arg_text):
    """CompileTextExport: strtok_s twice, then FUN_140490960 twice."""
    first, remainder = _strtok_space(arg_text, limit=2)
    errors = []
    if len(first) < 2:
        return first, errors + ["requires <ESMFilename> and <Language>"]
    # The CK explicitly requires the first two strtok_s results.
    esm, language = first[0], first[1]
    rest = remainder
    path, rest = _quoted_helper(rest)
    if path is None or path == "":
        errors.append("requires non-empty <PathToTextExport>")
        return [esm, language], errors
    backup, rest = _quoted_helper(rest)
    args = [esm, language, path]
    if backup is not None:
        args.append(backup)
    if rest.strip():
        errors.append("trailing arguments after optional <PathToBackupTextExport>")
    return args, errors


def _parse_quoted_args(arg_text):
    """GenerateSingleLip: FUN_140490960 twice, with no trailing tokens."""
    args = []
    rest = arg_text
    for label in ("WAVFilePath", "Text For WAV File"):
        token, rest = _quoted_helper(rest)
        if token is None:
            return args, ["requires two quoted-helper tokens: %s is missing" % label]
        args.append(token)
    if rest.strip():
        return args, ["trailing arguments after the two quoted-helper tokens"]
    return args, []


def _atoi_u32(token):
    """Approximate the dispatcher's atoi-to-uint conversion.

    The native code calls atoi(), then stores the result in uint locals. It is
    deliberately less strict than a decimal parser: a numeric prefix such as
    ``2junk`` becomes 2, while a token with no numeric prefix becomes 0.
    """
    m = re.match(r"^[ \t]*([+-]?)([0-9]+)", token)
    if not m:
        return 0
    value = int(m.group(2))
    if m.group(1) == "-":
        value = -value
    return value & 0xFFFFFFFF


def _validate_batch_args(rule, args):
    errors = []
    warnings = []
    if rule == "platform":
        if args[1].lower() not in _PLATFORM_TOKENS:
            errors.append("generation target '%s' must be XB1, X64, PS4, or W32" % args[1])
    elif rule == "mapinfo":
        if args[2].lower() not in _MAPINFO_KIND:
            errors.append("MapInfo kind '%s' must be doors, markers, or interiors" % args[2])
    elif rule == "worldspace":
        if args[1].lower() not in _WORLDSPACE_KIND:
            errors.append("MapMaker kind '%s' must be Worldspace or Interior" % args[1])
    elif rule == "animinfo":
        options = [a.lower() for a in args[3:]]
        unknown = [a for a in options if a not in _ANIMINFO_OPTIONS]
        if unknown:
            errors.append("unknown GenerateAnimInfo option(s): %s" % ", ".join(unknown))
        if len(options) != len(set(options)):
            errors.append("GenerateAnimInfo options may occur only once")
    elif rule == "previs":
        # The dispatcher initializes mode=filtered, scope=all, current=1,
        # total=1. It only enters the common chunk parser for an explicit
        # all/main/ints scope; that parser's missing-pair fallback is current=0,
        # total=1. Therefore ESM, ESM+mode, and ESM+mode+scope are all accepted.
        if len(args) == 1:
            return errors, warnings
        if args[1].lower() not in _PREVIS_CLEAN:
            errors.append("PreCombined/PreVis mode '%s' must be clean or filtered" % args[1])
            return errors, warnings
        if len(args) == 2:
            return errors, warnings
        scope = args[2].lower()
        if scope not in _PREVIS_SCOPE:
            errors.append("PreCombined/PreVis scope '%s' must be all, main, ints, or other" % args[2])
            return errors, warnings

        # 'other' sets scope=2 and jumps past the chunk parser. Any following
        # words are consequently ignored by the native dispatcher.
        if scope == "other":
            if len(args) > 3:
                warnings.append("currentset/totalsetcount are ignored for scope 'other'")
            return errors, warnings

        # all/main/ints enter the common parser. It consumes the next two
        # tokens only when both are present. One token is silently left with
        # the internal defaults (current=0,total=1), and tokens after the
        # fifth are not inspected.
        if len(args) == 4:
            warnings.append("currentset without totalsetcount is ignored; native defaults are used")
            return errors, warnings
        if len(args) >= 5:
            current = _atoi_u32(args[3])
            total = _atoi_u32(args[4])
            if total == 0 or current == 0 or total < current:
                errors.append("PreCombined/PreVis chunk must satisfy native check: currentset != 0, totalsetcount != 0, currentset <= totalsetcount")
            if len(args) > 5:
                warnings.append("tokens after totalsetcount are ignored by the native parser")
    return errors, warnings


def lint_batch_command(command):
    """Lint one CK batch command and return (ok, lines).

    The parser intentionally treats only the first recognized switch as the
    selected invocation, then rejects a second recognized switch because the
    CK's parser honors only one switch per invocation.
    """
    matches = []
    for idx, (name, lo, hi, rule) in enumerate(_BATCH_SPECS):
        pattern = re.compile(r"(?<![A-Za-z0-9_-])" + re.escape(name) + r":",
                             re.IGNORECASE)
        for match in pattern.finditer(command):
            matches.append((match.start(), match, idx, name, lo, hi, rule))
    matches.sort(key=lambda item: item[0])
    if not matches:
        return False, ["ERROR: no recognized -Switch: prefix found"]

    _, match, idx, name, lo, hi, rule = matches[0]
    errors = []
    warnings = []
    if len(matches) > 1:
        other = matches[1][3]
        errors.append("multiple batch switches: CK honors only the first (%s); found %s" %
                      (name, other))

    arg_text = command[match.end():]
    if rule == "compile":
        args, parse_errors = _parse_compile_args(arg_text)
    elif rule == "quoted":
        args, parse_errors = _parse_quoted_args(arg_text)
    else:
        args, _ = _strtok_space(arg_text)
        parse_errors = []
        if any('"' in token for token in args):
            warnings.append("this branch uses strtok_s on spaces; quotes are not removed")

    if len(args) < lo:
        errors.append("%s requires at least %d argument(s), got %d" % (name, lo, len(args)))
    # PreCombined/PreVis is the one branch whose native parser does not
    # inspect trailing tokens; its scope decides whether chunk tokens are read.
    if rule != "previs" and len(args) > hi:
        errors.append("%s accepts at most %d argument(s), got %d" % (name, hi, len(args)))
    errors.extend(parse_errors)
    if len(args) >= lo and (rule == "previs" or len(args) <= hi):
        rule_errors, rule_warnings = _validate_batch_args(rule, args)
        errors.extend(rule_errors)
        warnings.extend(rule_warnings)

    bits = 1 << idx
    tokenizer = ("raw slot" if name.lower() == "-exportdialogue" else
                 "FUN_140490960" if rule in ("compile", "quoted") else
                 "strtok_s(' ')")
    lines = ["BATCH: %s (index %d, bit 0x%x, tokenizer=%s)" %
             (name, idx, bits, tokenizer),
             "ARGS: %s" % (repr(args) if args else "(none)")]
    lines.extend("ERROR: " + item for item in errors)
    lines.extend("WARNING: " + item for item in warnings)
    if not errors:
        lines.append("VERDICT: PASS")
    else:
        lines.append("VERDICT: FAIL")
    return not errors, lines


# ---------------------------------------------------------------------------
# Exit-code matrix oracle (doc 19)
# ---------------------------------------------------------------------------

def _find_ini_value(sections, section_name, key_name):
    """Case-insensitive GetPrivateProfileString-style lookup."""
    for section, values in sections.items():
        if section.lower() != section_name.lower():
            continue
        for key, value in values.items():
            if key.lower() == key_name.lower():
                return value
    return None


def _exit_gate(path):
    """Return (raw value, uint parse, enabled) for the doc-19 gate setting."""
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        sections = parse_ini(fh.read())
    raw = _find_ini_value(sections, "General", "bThrowProcessCompleteExceptions")
    parsed = parse_int_win(raw) if raw is not None else 0
    return raw, parsed, parsed != 0


def report_exit_codes(path):
    """Print doc 19's 31-switch sentinel matrix for one CreationKit.ini.

    This predicts the *contract* codes reachable after a branch reports its
    result; it cannot know whether that branch's Execute/direct worker will
    succeed without running the CK and loading its data files.
    """
    try:
        raw, parsed, enabled = _exit_gate(path)
    except OSError as exc:
        print("ERROR: cannot read CreationKit.ini: %s" % exc)
        return 2

    print("EXIT-CODE ORACLE: doc 19 / 31-switch matrix")
    if raw is None:
        print("GATE: bThrowProcessCompleteExceptions:General = <missing> (OFF; default 0)")
    else:
        print("GATE: bThrowProcessCompleteExceptions:General = %r (parsed %d, %s)" %
              (raw, parsed, "ON" if enabled else "OFF"))
    print("CODES: success=0x6b650000 (1800404992), failure=0x6b650001 (1800404993)")
    print("NOTE: success/failure is runtime-dependent; this is the reachable exit contract.")
    print()

    for idx, (name, _lo, _hi, _rule) in enumerate(_BATCH_SPECS):
        bit = 1 << idx
        process = _EXIT_PROCESS_NAMES.get(idx, "unknown")
        if idx in _EXIT_NO_RAISE:
            behavior = "normal exit 0; no sentinel raise"
            detail = "doc 19 marks this branch as non-raising"
        elif idx in _EXIT_DIRECT:
            behavior = "0x6b650000 success / 0x6b650001 failure"
            detail = "dispatcher direct raise"
        else:
            behavior = "0x6b650000 success / 0x6b650001 failure"
            detail = "shared runner raise"
        print("%2d  0x%08x  %-29s %-30s %-48s  %s" %
              (idx, bit, name, process, behavior, detail))

    if enabled:
        print("\nVERDICT: PASS (exception gate is enabled; sentinel matrix is active)")
        return 0
    print("\nVERDICT: FAIL (bThrowProcessCompleteExceptions:General is not enabled; "
          "all sentinel raises are skipped)")
    return 1


# ---------------------------------------------------------------------------
# The oracle
# ---------------------------------------------------------------------------

def check_key(desc, inivalue, inisections, s_language):
    """Predict the engine's read outcome for one descriptor against the file.

    Returns (verdict, note). inivalue is the value found under the
    descriptor's section (None if the key is missing from the ini).
    """
    letter = desc["letter"]
    code, name = TYPE_LETTERS.get(letter, (9, "unknown"))
    section = desc["section"]

    # sLanguage indirection: sections named LANGUAGE use the sLanguage value.
    actual_section = section
    if section.lower() == "language":
        lang = inisections.get("General", {}).get("sLanguage")
        if lang is None or not lang.strip():
            lang = DEFAULT_SLANGUAGE
        actual_section = lang
        if inivalue is not None:
            return ("READ", "sLanguage-quirk section = '%s'" % lang)

    if inivalue is None:
        return ("MISSING", "default retained")

    if code == 9:
        n = parse_int_win(inivalue)
        return ("TYPE9-AS-UINT", "unknown letter '%s' -> read as uint (0x%08x)" % (letter, n))

    if code == 6:      # s / S
        return ("STR-READ", "raw string (%d chars)" % len(inivalue))

    if code == 5:      # f
        if not inivalue.strip():
            return ("READ-FAIL", "empty value -> default retained, no notify")
        if not sscanf_float(inivalue):
            return ("READ-FAIL", "sscanf %%f fails -> default retained, no notify")
        return ("READ", "float parsed")

    if code in (7, 8):  # r / a
        need = 3 if code == 7 else 4
        if not inivalue.strip():
            return ("READ-FAIL", "empty value -> read fails (default retained)")
        got, vals = sscanf_uint_list(inivalue, need)
        if got != need:
            # sscanf leaves unconverted args untouched (stack garbage); the
            # reader still packs and stores, and still fires the notify.
            vals += [0] * (need - len(vals))
            alpha = 0xFF if code == 7 else vals[3]
            packed = pack_bgra(vals[0], vals[1], vals[2], alpha)
            note = ("only %d/%d %%u conversions; record OVERWRITTEN with "
                    "partial pack 0x%08x + notify still fires (engine quirk)"
                    % (got, need, packed))
            return ("READ-FAIL", note)
        if code == 7:
            packed = pack_bgra(*vals, 0xFF)
        else:
            packed = pack_bgra(vals[0], vals[1], vals[2], vals[3])
        return ("READ", "parsed %s, packed BGRA 0x%08x" % (name, packed))

    # integer family (b/c/h/i/u) -- GetPrivateProfileIntA, never fails
    n = parse_int_win(inivalue)
    if not inivalue.strip() or not re.match(r"^[ \t]*[+-]?[0-9]", inivalue):
        return ("READ-AS-ZERO", "non-numeric '%s' -> engine reads 0" % inivalue.strip())
    if code == 0:
        return ("READ", "bool = %s" % ("true" if n != 0 else "false"))
    if code == 1:
        v = n & 0xFF
        return ("READ", "int8 = %d" % (v if v < 0x80 else v - 0x100))
    if code == 2:
        v = n & 0xFFFF
        return ("READ", "int16 = %d" % (v if v < 0x8000 else v - 0x10000))
    if code == 3:
        v = n & 0xFFFFFFFF
        return ("READ", "int32 = %d" % (v if v < 0x80000000 else v - 0x100000000))
    return ("READ", "uint32 = %d" % (n & 0xFFFFFFFF))


def validate(path, descriptors_path=None, s_language=None, strict=False):
    by_key, by_lower, descs = parse_descriptors(descriptors_path or DEFAULT_DESCRIPTORS)
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    inisections = parse_ini(text)

    s_language = s_language or DEFAULT_SLANGUAGE

    results = []          # (descriptor, verdict, note)
    seen = set()

    # the sLanguage indirection: a descriptor whose section is LANGUAGE is
    # actually read from [<value of [General] sLanguage>]
    lang_value = (inisections.get("General", {}).get("sLanguage") or "").strip()
    if not lang_value:
        lang_value = s_language or DEFAULT_SLANGUAGE
    lang_lower = lang_value.lower()

    # 1) every key the file contains, matched against the table
    for section, keys in inisections.items():
        for key, value in keys.items():
            d = by_lower.get((section.lower(), key.lower()))
            if d is None and section.lower() == lang_lower:
                # maybe a LANGUAGE-registered key read via the sLanguage quirk
                d = by_lower.get(("language", key.lower()))
            if d is None:
                note = "no descriptor"
                # known key under the wrong section: the engine looks up the
                # (section,key) pair, so it never reads this one
                others = [o for o in by_lower.values()
                          if o["key"].lower() == key.lower()]
                if others:
                    secs = ", ".join(sorted(set(o["section"] for o in others)))
                    note = "known key; registered under [%s] - never read here" % secs
                results.append(({"key": key, "section": section,
                                 "letter": key[:1] if key else "",
                                 "descriptor": "%s:%s" % (key, section)},
                                "UNKNOWN", note))
                seen.add((section.lower(), key.lower()))
                continue
            verdict, note = check_key(d, value, inisections, s_language)
            results.append((d, verdict, note))
            seen.add((section.lower(), key.lower()))

    # 2) descriptors the file does not contain (missing -> default)
    for d in descs:
        if (d["section"].lower(), d["key"].lower()) not in seen:
            results.append((d, "MISSING", "default retained"))

    return results, inisections


def fmt_result(d, verdict, note):
    return "%-14s %-28s [%-12s] %s  %s" % (
        verdict, d["key"], d["section"], d.get("letter", ""), note)


def main(argv):
    ap = argparse.ArgumentParser(
        description="CreationKit.ini compatibility oracle (reader FUN_1425c7f90)")
    ap.add_argument("file", nargs="?", help="CreationKit.ini to validate")
    ap.add_argument("--descriptors", default=None,
                    help="descriptor table (default: ghidra-dump/ck-settings-keys.txt)")
    ap.add_argument("--sLanguage", default=None,
                    help="override the [General] sLanguage value used for LANGUAGE sections")
    ap.add_argument("--strict", action="store_true",
                    help="treat UNKNOWN/READ-AS-ZERO/MISSING as failures")
    ap.add_argument("--missing", action="store_true",
                    help="list every descriptor key absent from the file (default: count only)")
    ap.add_argument("--list-keys", action="store_true",
                    help="print all descriptor keys as a reference card")
    ap.add_argument("--stats", action="store_true",
                    help="print type-letter and section distribution")
    ap.add_argument("--command", "--batch-command", dest="batch_command",
                    metavar="CMD",
                    help="lint one CK batch command against the doc-05/13 grammar (use --command= for a no-arg switch)")
    ap.add_argument("--exit-codes", action="store_true",
                    help="replay doc 19's 31-switch exit-code matrix; requires the INI gate to be enabled")
    args = ap.parse_args(argv)

    if args.batch_command is not None:
        ok, lines = lint_batch_command(args.batch_command)
        print("\n".join(lines))
        return 0 if ok else 1

    if args.exit_codes:
        if not args.file:
            ap.error("--exit-codes requires a CreationKit.ini file")
        return report_exit_codes(args.file)

    if args.list_keys:
        by_key, by_lower, descs = parse_descriptors(args.descriptors or DEFAULT_DESCRIPTORS)
        rows = sorted(descs, key=lambda d: (d["section"].lower(), d["key"].lower()))
        print("// %d descriptor keys (type letter = first char)" % len(rows))
        for d in rows:
            print("%-6s %-26s : %s" % (type_name(d["letter"]), d["key"], d["section"]))
        return 0

    if args.stats:
        by_key, by_lower, descs = parse_descriptors(args.descriptors or DEFAULT_DESCRIPTORS)
        from collections import Counter
        letters = Counter(d["letter"] for d in descs)
        sections = Counter(d["section"] for d in descs)
        print("type letters: " + ", ".join("%s=%d" % (c, n) for c, n in sorted(letters.items())))
        print("sections (%d):" % len(sections))
        for sec, n in sections.most_common():
            print("  %-24s %d" % (sec, n))
        return 0

    if not args.file:
        ap.error("a CreationKit.ini file is required (or --command / --list-keys / --stats)")

    results, inisections = validate(args.file, args.descriptors, args.sLanguage,
                                    args.strict)

    order = {"READ": 0, "STR-READ": 0, "READ-AS-ZERO": 1, "TYPE9-AS-UINT": 1,
             "MISSING": 1, "UNKNOWN": 2, "READ-FAIL": 3, "WRONG-SECTION": 3}
    by_v = {}
    for d, v, note in results:
        by_v.setdefault(v, []).append((d, note))

    for v in sorted(by_v, key=lambda x: order.get(x, 9)):
        rows = by_v[v]
        print("== %s (%d) ==" % (v, len(rows)))
        if v == "MISSING" and not args.missing:
            print("  (hidden; pass --missing to list all %d)" % len(rows))
            print()
            continue
        for d, note in sorted(rows, key=lambda r: (r[0]["section"], r[0]["key"])):
            print(fmt_result(d, v, note))
        print()

    n_fail = len(by_v.get("READ-FAIL", [])) + len(by_v.get("WRONG-SECTION", []))
    n_warn = (len(by_v.get("UNKNOWN", [])) + len(by_v.get("READ-AS-ZERO", []))
              + len(by_v.get("TYPE9-AS-UINT", [])) + len(by_v.get("MISSING", [])))
    total = len(results)
    n_file = sum(len(v) for v in inisections.values())
    print("SUMMARY: %d keys checked (%d in file, %d missing descriptors), "
          "%d failures, %d warnings" % (total, n_file, total - n_file, n_fail, n_warn))
    verdict = "FAIL" if n_fail or (args.strict and n_warn) else "PASS"
    print("VERDICT: %s (exit %d)" % (verdict, 1 if verdict == "FAIL" else 0))
    return 1 if verdict == "FAIL" else 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except BrokenPipeError:
        # stdout closed early (e.g. piped to head)
        try:
            sys.stdout.close()
        except Exception:
            pass
        sys.exit(0)
