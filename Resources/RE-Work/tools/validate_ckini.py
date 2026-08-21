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
    python3 validate_ckini.py --compile-text \
        --command '-CompileTextExport:Fallout4.esm ENGLISH backup primary'
    python3 validate_ckini.py --compile-text samples/compile-text/backup samples/compile-text/primary
    # each run also prints ROUTE lines + a ROUTING SUMMARY predicting the
    # .STRINGS / .DLSTRINGS / .ILSTRINGS output table per record
    # combine with --strings to cross-check predictions against real outputs:
    python3 validate_ckini.py --compile-text samples/compile-text/backup \
        samples/compile-text/primary --strings '.../STRINGS'
    python3 validate_ckini.py CreationKit.ini --exit-codes
    python3 validate_ckini.py --strings \
        '.../STRINGS/Fallout4_en.STRINGS' '.../STRINGS/Fallout4_en.DLSTRINGS'
    python3 validate_ckini.py --strings '.../STRINGS'   # whole language dir
    # attribute compiled IDs back to their TextExport categories, so the
    # partition report also counts how many IDs per table came from each
    # dialog category (indices {1,6,7,10} -> .DLSTRINGS):
    python3 validate_ckini.py --strings '.../STRINGS' \
        --text-export '.../TextExport.txt' ['.../more/TextExport.txt' ...]

The descriptor table maps 1,629 colon-bearing `bKey:Section` literals plus
25 byte-verified bare records (whose reader section is `[MAIN]`; doc 10 counts
1,641 colon-bearing records with static pointers). The type letter is the
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
import struct
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

# The binary contains 25 INI SettingT records whose key literals have no
# ``:Section`` suffix. FUN_1425c6690 supplies the reader's literal fallback
# section ``MAIN`` for these records. They are not present in
# ck-settings-keys.txt because that dump is the 1,629 colon-bearing literal
# inventory; keep this correction here so the oracle's default schema matches
# the actual descriptor records without changing the raw string dump.
NO_SECTION_DESCRIPTORS = (
    "iBatchWindowMaxRefSelectedRefresh",
    "uFollowingCompanionFailsBeforeWarpAttempt",
    "fFollowingCompanionFailsAtDistanceRatio",
    "fFollowingCompanionPreferredWarpDistance",
    "bEnableLipLookup",
    "bSkipFurnitureMarkerVisualization",
    "bPrimitivesOn",
    "bUseWaterHDR",
    "fPrecutMinStepHeight",
    "sControlsRemapFile",
    "fMaxBoneSpeedBeforeDefaultStiffness",
    "bDefaultLandscape_WetnessControl_UseScreenSpaceReflections",
    "fDefaultLandscape_WetnessControl_SpecScale",
    "fDefaultLandscape_WetnessControl_SpecPowerScale",
    "fDefaultLandscape_WetnessControl_SpecMin",
    "fDefaultLandscape_WetnessControl_EnvMapScale",
    "fDefaultLandscape_WetnessControl_FresnelPower",
    "fDefaultLandscape_WetnessControl_Metalness",
    "uMaxRainSplashPerFrame",
    "uRainSplashBufferSize",
    "bRainSplash",
    "fMinSplashSize",
    "fMaxSplashSize",
    "fDensityDistModifier",
    "fSkyCellRefFadeDistance",
)


# ---------------------------------------------------------------------------
# Descriptor table
# ---------------------------------------------------------------------------

def parse_descriptors(path, include_no_section=None):
    """Parse the descriptor literal list into a lookup table.

    A line containing ``:`` is split as ``key:section``. A bare line is
    assigned the engine reader's fallback section ``MAIN``. For the shipped
    CK descriptor dump, the 25 byte-verified bare records are appended from
    ``NO_SECTION_DESCRIPTORS``; custom fixture tables are left untouched.

    Returns (by_key, by_lower, descs) where:
      by_key   = {(section, key): descriptor-dict}   (canonical case)
      by_lower = {(section_lower, key_lower): descriptor-dict}
    """
    if include_no_section is None:
        include_no_section = (os.path.abspath(path) ==
                              os.path.abspath(DEFAULT_DESCRIPTORS))
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

    if include_no_section:
        present = {(d["section"].lower(), d["key"].lower()) for d in descs}
        for key in NO_SECTION_DESCRIPTORS:
            if ("main", key.lower()) not in present:
                descs.append({"key": key, "section": "MAIN",
                              "letter": key[:1] if key else "",
                              "descriptor": key})

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

# The 18-category table FUN_14091f3d0 walks at .rdata 0x143b9bef0 (doc 05 /
# decompile-CompileTextExport-line-parser-FUN_14091f3d0.md). Index 17 (Misc)
# is the parser's default when no _stricmp match is found.
_TEXT_CATEGORIES = [
    "Response Text", "Quest Stage Log Entry", "Quest Objective Display Text",
    "Name", "Topic Text", "Game Setting", "Book", "Description",
    "Faction Rank", "Note", "Message", "Message Box Button",
    "Ammo Short Name", "Body Part Name", "Alternate Activation Text",
    "Activate Choice Label", "Loading Screen Text", "Misc",
]
_TEXT_CATEGORIES_LOWER = [c.lower() for c in _TEXT_CATEGORIES]

# The caller's ``0x4c2`` bit test (FUN_140916980) sends exactly these four
# category indices to the dialog context; every other category goes to the
# ordinary context. The third .ILSTRINGS context is fed by the supplemental
# ID-named input files, not by a category name (doc 05 /
# decompile-CompileTextExport-FUN_140916980.md).
_TEXT_DIALOG_INDICES = {1, 6, 7, 10}

# The parser's "reserved ID" prefix check: the engine skips the low 16 IDs
# (0x00000000..0x0000000F, high 28 bits zero) with
# ``Attempting to use reserved ID 0x%08X. Skipping.``
_TEXT_RESERVED_LIMIT = 0x10

# Doc 05 / FUN_14091f3d0: the reader replaces the first three tab bytes with
# NUL delimiters. A record without three tabs has no text field and is parsed
# as a malformed line; the producer emits exactly three tabs.
_TEXT_TAB_COUNT = 3

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


_COMPILE_FIXTURE_README = ("CompileTextExport input fixtures live in "
                            "tools/samples/compile-text/ (see its README.txt): "
                            "primary and backup roots, mandatory TextExport.txt "
                            "files, 8-hex-stem supplemental files, a malformed "
                            "supplemental filename, duplicate IDs across roots, "
                            "a reserved-ID record, an unknown category, and a "
                            "malformed line.")


def _text_root_record(raw_line):
    """Return (string_id_or_None, category_or_None, notes).

    Mirrors the four-field TextExport.txt record parser FUN_14091f3d0:
    the first three tabs are structural delimiters, the StringID is parsed
    as hexadecimal, an invalid/zero StringID yields no usable ID, and an
    unknown category falls back to index 17 (Misc). Tabs after the third
    delimiter are ordinary text. The caller decides how unknown categories
    are routed; here they are surfaced as notes, never as errors.
    """
    parts = raw_line.split("\t")
    if len(parts) < _TEXT_TAB_COUNT + 1:
        return None, None, ["malformed line (fewer than %d tab fields)" % _TEXT_TAB_COUNT]
    notes = []
    m = re.match(r"^0x([0-9A-Fa-f]+)$", parts[0].strip())
    if not m:
        return None, None, notes + ["StringID field '%s' is not 0x<hex>" % parts[0].strip()]
    string_id = int(m.group(1), 16) & 0xFFFFFFFF
    category = parts[2].strip()
    if category.lower() not in _TEXT_CATEGORIES_LOWER:
        notes.append("unknown category '%s' -> falls back to Misc" % category)
    return string_id, category, notes


def _supplemental_id_from_filename(name):
    """Derive the supplemental-file ID exactly like FUN_140916980.

    The worker strips the final extension and requires the remaining stem to
    be exactly 8 hexadecimal characters. Returns (id, None) or (None, reason).
    """
    stem = name.rsplit(".", 1)[0]
    if len(stem) != 8:
        return None, "stem '%s' is not 8 characters" % stem
    if not re.fullmatch(r"[0-9A-Fa-f]{8}", stem):
        return None, "stem '%s' is not 8 hexadecimal characters" % stem
    return int(stem, 16), None


def _esm_stem(esm):
    """The output-name stem: input ESM name cut at its final dot (strrchr)."""
    return esm.rsplit(".", 1)[0]


def _compile_output_name(esm, language, ext):
    """Output path Data/STRINGS/<stem>_<Language><ext> (doc 05 / FUN_14091aa30).

    ``ext`` carries its leading dot (``.STRINGS`` etc.); the engine builds
    the path as ``Data/%%s/%%s_%%s%%s`` (FUN_14091aa30).
    """
    return "Data\\STRINGS\\%s_%s%s" % (_esm_stem(esm), language, ext)


def _compile_route(category, from_supplemental, esm, language):
    """Predict the localized-string output table for one text record.

    TextExport.txt records are routed by category: the dialog bitmask
    indices {1,6,7,10} land in .DLSTRINGS, everything else in .STRINGS
    (unknown categories fall back to Misc -> ordinary). Supplemental
    ID-named files feed the .ILSTRINGS context regardless of category.
    Returns (extension, output filename) for the record.
    """
    if from_supplemental:
        ext = ".ILSTRINGS"
    else:
        cat = (category or "").strip().lower()
        try:
            idx = _TEXT_CATEGORIES_LOWER.index(cat)
        except ValueError:
            idx = len(_TEXT_CATEGORIES) - 1   # Misc default (index 17)
        ext = ".DLSTRINGS" if idx in _TEXT_DIALOG_INDICES else ".STRINGS"
    return ext, _compile_output_name(esm, language, ext)


def check_compile_text_roots(esm, roots, language=None):
    """Validate the CompileTextExport input-root set like FUN_140916980.

    Returns (ok, lines). For each root (traversed in the engine's
    ``[backup,] primary`` order) the mandatory ``<root>/<ESM>/TextExport.txt``
    must exist; supplemental ``*.txt`` files must carry an 8-hex-character
    ID stem. Records are parsed with the four-field grammar: reserved IDs,
    duplicate IDs, unknown categories, and malformed lines are the engine's
    skip/recovery diagnostics (warnings), not hard failures.

    ``language`` selects the output filename; when omitted (positional-root
    mode) the DEFAULT_SLANGUAGE value is used so the routing predictions
    still carry a concrete path.
    """
    if language is None:
        language = DEFAULT_SLANGUAGE
    lines = ["COMPILE-TEXT: FUN_140916980 input-root contract"]
    if not esm:
        lines.append("ERROR: no <ESMFilename> for the input path <root>/<ESM>/TextExport.txt")
        return False, lines
    if not roots:
        lines.append("ERROR: no input roots")
        return False, lines
    lines.append("ESM: %s" % esm)
    lines.append("LANGUAGE: %s" % language)
    lines.append("ROOTS (traversal order [backup,] primary): %s" % ", ".join(roots))

    seen_ids = {}          # string_id -> (label, line) first seen
    reserved = []
    duplicates = []
    malformed_lines = []
    unknown_categories = []
    errors = []
    warnings = []
    routes = []            # per-record output-table predictions
    route_records = []     # structured {id, ext, source, category} for cross-check
    route_counts = {}      # extension -> record count

    for root in roots:
        root_dir = os.path.abspath(root)
        esm_dir = os.path.join(root_dir, esm)
        mandatory = os.path.join(esm_dir, "TextExport.txt")
        if not os.path.isfile(mandatory):
            errors.append("mandatory input missing: %s" % mandatory)
            continue
        lines.append("ROOT %s: mandatory %s" % (root, mandatory))

        # The worker derives each supplemental file's ID from its 8-hex stem
        # and skips malformed names entirely (``Could not parse ID from
        # filename: %s. Skipping.``); such files are never read. The third
        # tuple slot marks the .ILSTRINGS feeder: supplemental ID-named files
        # (TextExport.txt is the category-routed mandatory input).
        reads = [("TextExport.txt", mandatory, False)]
        try:
            names = sorted(f for f in os.listdir(esm_dir)
                           if f.lower().endswith(".txt") and f != "TextExport.txt")
        except OSError as exc:
            errors.append("could not list %s: %s" % (esm_dir, exc))
            continue
        for name in names:
            file_id, reason = _supplemental_id_from_filename(name)
            if reason:
                warnings.append("Could not parse ID from filename: %s. Skipping (%s)" % (name, reason))
                continue
            lines.append("  supplemental %s -> file ID 0x%08X" % (name, file_id))
            reads.append((name, os.path.join(esm_dir, name), True))

        for fname, path, is_supplemental in reads:
            label = "%s/%s" % (os.path.basename(root), fname)
            try:
                with open(path, "r", encoding="utf-8", errors="replace") as fh:
                    for lineno, raw in enumerate(fh, 1):
                        line = raw.rstrip("\r\n")
                        if not line.strip() or line.lstrip().startswith(";"):
                            continue
                        string_id, category, notes = _text_root_record(line)
                        # The unknown-category fallback is its own warning;
                        # structural problems (bad StringID, missing tabs)
                        # are the malformed-line diagnostics.
                        structural = [n for n in notes
                                      if not n.startswith("unknown category")]
                        if structural:
                            malformed_lines.append("%s:%d: %s" % (label, lineno, "; ".join(structural)))
                        if string_id is None:
                            continue
                        # Reserved IDs are skipped before category routing,
                        # exactly like the engine's reserved-ID branch.
                        if string_id < _TEXT_RESERVED_LIMIT:
                            reserved.append("%s:%d: Attempting to use reserved ID 0x%08X. Skipping." %
                                            (label, lineno, string_id))
                            continue
                        if category and category.lower() not in _TEXT_CATEGORIES_LOWER:
                            unknown_categories.append("%s:%d: unknown category '%s' -> falls back to Misc" %
                                                      (label, lineno, category))
                        first = seen_ids.get(string_id)
                        if first is not None:
                            duplicates.append("Duplicate ID found: 0x%08X (%s:%d; first seen %s)" %
                                              (string_id, label, lineno, first))
                        else:
                            seen_ids[string_id] = "%s:%d" % (label, lineno)
                        # Output-table prediction (doc 05 / FUN_140916980
                        # ``0x4c2`` dialog bitmask + supplemental .ILSTRINGS):
                        # the compiler would write this record to exactly one
                        # of the three Data\STRINGS files.
                        ext, outname = _compile_route(category, is_supplemental,
                                                      esm, language)
                        routes.append("%s:%d: 0x%08X [%s] -> %s"
                                      % (label, lineno, string_id,
                                         (category or "").strip() or "(none)", outname))
                        route_records.append({
                            "id": string_id,
                            "ext": ext,
                            "source": "%s:%d" % (label, lineno),
                            "category": (category or "").strip() or "(none)",
                        })
                        route_counts[ext] = route_counts.get(ext, 0) + 1
            except OSError as exc:
                errors.append("could not read %s: %s" % (path, exc))

    for item in reserved:
        warnings.append(item)
    for item in duplicates:
        warnings.append(item)
    for item in unknown_categories:
        warnings.append(item)
    for item in malformed_lines:
        warnings.append(item)
    if not errors:
        warnings.append("all diagnostics above are skip/recovery paths; the aggregate "
                        "string table is built from %d unique IDs" % len(seen_ids))

    lines.extend("ERROR: " + item for item in errors)
    lines.extend("WARNING: " + item for item in warnings)

    # Output-table prediction summary: every routed record lands in exactly
    # one of the three Data\STRINGS files (doc 05 / localized-string-file
    # format doc; FUN_140916980 ``0x4c2`` dialog bitmask for TextExport.txt
    # records, .ILSTRINGS for supplemental ID-named inputs).
    if routes:
        lines.append("ROUTING (predicted output tables):")
        lines.extend("  ROUTE: " + r for r in routes)
        total = sum(route_counts.values())
        lines.append("ROUTING SUMMARY: %d routed records -> %s" % (
            total, ", ".join("%s=%d" % (k, v)
                              for k, v in sorted(route_counts.items()))))

    if not errors:
        lines.append("VERDICT: PASS")
    else:
        lines.append("VERDICT: FAIL")
    return not errors, lines, route_records


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
# Localized-string files (.STRINGS / .DLSTRINGS / .ILSTRINGS)
# ---------------------------------------------------------------------------
# Byte-level format documented in
# ghidra-dump/localized-string-file-format.md (writer FUN_14091d790, loader
# FUN_140915000):
#
#     offset  size  field
#     0x00    4     u32 numStrings
#     0x04    4     u32 dataSize
#     0x08    8*N   rows { u32 stringID; u32 offset; }   (N == numStrings)
#     0x08+8N  D    data block (D == dataSize)
#
# invariant: len(file) == 8 + 8*numStrings + dataSize
# table rows sorted ascending by stringID; offsets point into the data block
# (insertion order, not ID order; identical text dedups to one offset).
# .STRINGS      records:  NUL-terminated text, offset -> first byte
# .DL/.ILSTRINGS records:  u32 len + text (len INCLUDES the trailing NUL),
#                          offset -> the u32 len; block walks to EOF as
#                          sum(4 + len).

_STRINGS_KINDS = (".STRINGS", ".DLSTRINGS", ".ILSTRINGS")
_STRINGS_MAX_ROWS = 10_000_000

# Shipped Fallout 4 files (all 11 languages) contain exactly two stringIDs
# that live in BOTH the .STRINGS and .DLSTRINGS tables: the Enclave
# Creation-Club weapon/armor paint descriptions (English text in .STRINGS,
# localized text in .DLSTRINGS). The engine tolerates the collision; the
# "globally unique" rule is a writer invariant with these two known shipped
# exceptions.
_STRINGS_KNOWN_PARTITION_EXCEPTIONS = {0x36CB7, 0x36CB9}


def _strings_kind(path):
    """Return one of _STRINGS_KINDS (upper-cased) or None for a foreign file."""
    base = os.path.basename(path)
    upper = base.upper()
    for kind in _STRINGS_KINDS:
        if upper.endswith(kind):
            return kind
    return None


def _strings_lang_stem(path):
    """Group key: the '<ESM>_<Language>' stem shared by the three kinds."""
    base = os.path.basename(path)
    return base.rsplit(".", 1)[0].lower()


def load_text_export_categories(paths):
    """Build {stringID: canonical-category} from TextExport.txt file(s).

    Reuses the four-field FUN_14091f3d0 record parser. Reserved IDs (below
    0x10) are skipped exactly like the compiler, so they never show up as
    attributed. Category names are canonicalized (case-insensitive match,
    unknown -> Misc, the parser's index-17 default), so the dialog lookups
    in the composition report are exact. A directory argument is walked
    recursively for files named TextExport.txt. Unreadable paths surface as
    warnings, not failures -- the map only enriches the --strings partition
    report.

    Returns (id_to_category, warnings).
    """
    id_to_cat = {}
    warnings = []
    for p in paths:
        if os.path.isdir(p):
            names = []
            for root, _dirs, files in os.walk(p):
                for name in files:
                    if name.lower() == "textexport.txt":
                        names.append(os.path.join(root, name))
            if not names:
                warnings.append("no TextExport.txt found under %s" % p)
            files = sorted(names)
        else:
            files = [p]
        for path in files:
            try:
                with open(path, "r", encoding="utf-8", errors="replace") as fh:
                    for lineno, raw in enumerate(fh, 1):
                        line = raw.rstrip("\r\n")
                        if not line.strip() or line.lstrip().startswith(";"):
                            continue
                        string_id, category, notes = _text_root_record(line)
                        if string_id is None:
                            for n in notes:
                                if not n.startswith("unknown category"):
                                    warnings.append("%s:%d: %s"
                                                    % (os.path.basename(path), lineno, n))
                            continue
                        if string_id < _TEXT_RESERVED_LIMIT:
                            continue
                        cat = (category or "").strip()
                        try:
                            cat = _TEXT_CATEGORIES[_TEXT_CATEGORIES_LOWER.index(cat.lower())]
                        except ValueError:
                            cat = _TEXT_CATEGORIES[-1]   # Misc fallback
                        id_to_cat[string_id] = cat
            except OSError as exc:
                warnings.append("could not read %s: %s" % (path, exc))
    return id_to_cat, warnings


def check_strings_file(path):
    """Validate one localized-string file; return (ok, lines)."""
    kind = _strings_kind(path)
    lines = ["STRINGS %s [%s]" % (os.path.basename(path), kind)]
    if kind is None:
        return False, lines + ["ERROR: extension is not .STRINGS/.DLSTRINGS/.ILSTRINGS"]
    try:
        with open(path, "rb") as fh:
            data = fh.read()
    except OSError as exc:
        return False, lines + ["ERROR: %s" % exc]
    errors, warnings, notes = [], [], []

    if len(data) < 8:
        errors.append("file is %d bytes, shorter than the 8-byte header" % len(data))
        return False, lines + ["ERROR: " + e for e in errors]

    num_strings, data_size = struct.unpack_from("<II", data, 0)
    table_size = 8 * num_strings
    file_expected = 8 + table_size + data_size
    if num_strings > _STRINGS_MAX_ROWS:
        errors.append("numStrings %d exceeds the %d-row sanity bound"
                      % (num_strings, _STRINGS_MAX_ROWS))
    if file_expected != len(data):
        errors.append("size invariant violated: file is %d bytes, "
                      "8 + 8*numStrings(%d) + dataSize(%d) = %d"
                      % (len(data), num_strings, data_size, file_expected))
    if len(data) < 8 + table_size:
        errors.append("file ends inside the offset table "
                      "(need %d bytes for %d rows)" % (8 + table_size, num_strings))
    if errors:
        return False, lines + ["ERROR: " + e for e in errors]

    notes.append("numStrings=%d dataSize=%d (invariant holds)" % (num_strings, data_size))
    rows = [struct.unpack_from("<II", data, 8 + 8 * i) for i in range(num_strings)]
    ids = [r[0] for r in rows]

    # Table rows must be sorted ascending by stringID (writer FUN_1409186d0).
    if any(a >= b for a, b in zip(ids, ids[1:])):
        first_bad = next(i for i in range(1, len(ids))
                         if ids[i] <= ids[i - 1])
        warnings.append("offset table not strictly ascending by stringID "
                        "(first violation at row %d: 0x%08X -> 0x%08X)"
                        % (first_bad, ids[first_bad - 1], ids[first_bad]))
    elif num_strings > 1 and ids[0] == ids[-1]:
        warnings.append("all %d rows share one stringID 0x%08X"
                        % (num_strings, ids[0]))

    # Offsets must point inside the data block.
    data_block = data[8 + table_size:]
    bad_off = ["0x%08X@0x%x" % (r[0], r[1]) for r in rows if r[1] >= data_size]
    if bad_off:
        errors.append("%d offset(s) outside data block: %s"
                      % (len(bad_off), ", ".join(bad_off[:8])))
    unique_offs = sorted({r[1] for r in rows})

    if kind == ".STRINGS":
        # NUL-terminated: every offset must point at a string with a NUL
        # before the block end.
        if not errors:
            for off in unique_offs:
                nul = data_block.find(b"\x00", off)
                if nul < 0:
                    errors.append("no NUL terminator for offset 0x%x" % off)
                    break
        if not errors:
            lens = [data_block.find(b"\x00", off) - off for off in unique_offs]
            empty = sum(1 for l in lens if l == 0)
            notes.append("%d unique offsets, %d empty string(s), "
                         "max text length %d" % (len(unique_offs), empty, max(lens)))
    else:
        # Length-prefixed: offset must land on a record boundary and the data
        # block must walk to EOF as sum(4 + len). len INCLUDES the NUL.
        boundaries = {}
        pos = 0
        while pos < len(data_block) and not errors:
            if pos + 4 > len(data_block):
                errors.append("record header overruns data block at 0x%x" % pos)
                break
            rec_len = struct.unpack_from("<I", data_block, pos)[0]
            if pos + 4 + rec_len > len(data_block):
                errors.append("record at 0x%x (len %d) overruns data block"
                              % (pos, rec_len))
                break
            if data_block[pos + 4 + rec_len - 1] != 0:
                warnings.append("record at 0x%x (len %d) is not NUL-terminated"
                                % (pos, rec_len))
            boundaries[pos] = rec_len
            pos += 4 + rec_len
        if not errors and pos != len(data_block):
            errors.append("data block does not walk to EOF: ended at 0x%x of 0x%x"
                          % (pos, len(data_block)))
        if not errors:
            off_records = [r for r in rows if r[1] not in boundaries]
            if off_records:
                errors.append("%d table offset(s) not on a record boundary: %s"
                              % (len(off_records),
                                 ", ".join("0x%08X@0x%x" % r for r in off_records[:8])))
        if not errors:
            notes.append("%d unique offsets, %d data records, "
                         "block walks to EOF as sum(4+len)"
                         % (len(unique_offs), len(boundaries)))

    lines.extend("NOTE: " + n for n in notes)
    lines.extend("WARNING: " + w for w in warnings)
    lines.extend("ERROR: " + e for e in errors)
    if not errors:
        lines.append("VERDICT: PASS")
    else:
        lines.append("VERDICT: FAIL")
    return not errors, lines


def check_strings_files(paths, category_map=None):
    """Validate .STRINGS/.DLSTRINGS/.ILSTRINGS files (or directories of them).

    Returns (ok, lines). Directories are scanned for the three kinds. When
    several files share the same ``<ESM>_<Language>`` stem, their stringID
    sets are cross-checked for the global-uniqueness partition (a string ID
    lives in exactly one of the three tables).

    ``category_map`` (optional) maps stringID -> canonical TextExport
    category name (see ``load_text_export_categories``). When supplied, the
    per-stem report also counts, for every table, how many of its IDs were
    routed from each dialog category (indices {1,6,7,10} -> .DLSTRINGS per
    FUN_140916980's 0x4c2 bitmask) versus the ordinary categories.
    """
    files = []
    for p in paths:
        if os.path.isdir(p):
            for name in sorted(os.listdir(p)):
                full = os.path.join(p, name)
                if os.path.isfile(full) and _strings_kind(full):
                    files.append(full)
        else:
            files.append(p)
    if not files:
        return False, ["STRINGS: no .STRINGS/.DLSTRINGS/.ILSTRINGS files found"]

    lines = ["STRINGS: localized-string file validation (format doc: "
             "ghidra-dump/localized-string-file-format.md)"]
    by_stem = {}
    overall_ok = True
    for path in sorted(files):
        ok, sub = check_strings_file(path)
        lines.extend(sub)
        lines.append("")
        overall_ok = overall_ok and ok
        if _strings_kind(path):
            by_stem.setdefault(_strings_lang_stem(path), []).append(path)

    # Cross-file ID partition per language stem: a stringID must appear in
    # exactly one table. The routed-record composition (when TextExport
    # categories are available) is reported for every stem, including stems
    # that hold a single file.
    for stem, group in sorted(by_stem.items()):
        kind_ids = {k: [] for k in _STRINGS_KINDS}
        for path in group:
            kind = _strings_kind(path)
            with open(path, "rb") as fh:
                head = fh.read(8)
            if len(head) < 8:
                continue
            num, _ = struct.unpack_from("<II", head, 0)
            with open(path, "rb") as fh:
                data = fh.read()
            for i in range(num):
                sid, _ = struct.unpack_from("<II", data, 8 + 8 * i)
                kind_ids[kind].append(sid)

        if len(group) >= 2:
            seen = {}
            collisions = []
            for kind, ids in kind_ids.items():
                for sid in ids:
                    if sid in seen and seen[sid] != kind:
                        collisions.append("0x%08X in both %s and %s"
                                          % (sid, seen[sid], kind))
                    seen.setdefault(sid, kind)
            if collisions:
                known = [c for c in collisions
                         if int(c.split(" ")[0], 16) in _STRINGS_KNOWN_PARTITION_EXCEPTIONS]
                if len(known) == len(collisions):
                    lines.append("WARNING: STRINGS %s: %d known shipped exception(s) "
                                 "(Enclave Creation-Club paints, present in every language): %s"
                                 % (stem, len(collisions), "; ".join(collisions)))
                else:
                    overall_ok = False
                    lines.append("STRINGS %s: stringID partition violated (%d collision(s)): %s"
                                 % (stem, len(collisions), "; ".join(collisions[:6])))
            else:
                lines.append("STRINGS %s: %d files, stringID partition holds "
                             "(%d unique IDs)" % (stem, len(group), len(seen)))

        # Routed-record composition per table: how many of this stem's
        # compiled IDs came from each dialog category (indices {1,6,7,10})
        # versus the ordinary categories, attributed via TextExport records.
        if category_map:
            total = sum(len(ids) for ids in kind_ids.values())
            lines.append("STRINGS %s: TextExport routed-record composition "
                         "(%d IDs across %d file(s))" % (stem, total, len(group)))
            for kind in _STRINGS_KINDS:
                ids = kind_ids[kind]
                if not ids:
                    continue
                by_cat = {}
                for sid in ids:
                    cat = category_map.get(sid)
                    if cat is not None:
                        by_cat[cat] = by_cat.get(cat, 0) + 1
                attr = sum(by_cat.values())
                dialog = sum(by_cat.get(_TEXT_CATEGORIES[i], 0)
                             for i in _TEXT_DIALOG_INDICES)
                ordinary = attr - dialog
                detail = []
                if dialog:
                    parts = ["%s=%d" % (_TEXT_CATEGORIES[i], by_cat.get(_TEXT_CATEGORIES[i], 0))
                             for i in sorted(_TEXT_DIALOG_INDICES)
                             if by_cat.get(_TEXT_CATEGORIES[i])]
                    detail.append("dialog-routed=%d (%s)" % (dialog, ", ".join(parts)))
                if ordinary:
                    detail.append("ordinary-routed=%d" % ordinary)
                if not detail:
                    detail.append("none attributed")
                lines.append("  %s: %d IDs, %d attributed (%s), %d unattributed"
                             % (kind, len(ids), attr, "; ".join(detail),
                                len(ids) - attr))

    lines.append("VERDICT: %s" % ("PASS" if overall_ok else "FAIL"))
    return overall_ok, lines


def _load_strings_ids(path):
    """Return {stringID: kind} for one compiled localized-string file.

    Reuses the same header/table parse as check_strings_file; returns {}
    for unreadable or foreign files so callers can treat them as absent.
    """
    kind = _strings_kind(path)
    if kind is None:
        return {}
    try:
        with open(path, "rb") as fh:
            data = fh.read()
    except OSError:
        return {}
    if len(data) < 8:
        return {}
    num, _ = struct.unpack_from("<II", data, 0)
    if num > _STRINGS_MAX_ROWS or len(data) < 8 + 8 * num:
        return {}
    ids = {}
    for i in range(num):
        sid, _ = struct.unpack_from("<II", data, 8 + 8 * i)
        ids[sid] = kind
    return ids


def cross_check_compile_routes(route_records, strings_paths, esm, language):
    """Verify each predicted output table against real compiled string files.

    Returns (ok, lines). ``strings_paths`` may mix files and directories;
    only files whose ``<ESM>_<Language>`` stem matches the compile's esm/
    language (or the default language when none was given) are considered.
    For every routed record the ID must be present in the predicted table;
    a record found in a *different* table is a routing discrepancy (hard
    failure), and a record missing from all matching compiled files is a
    warning (the compiled set may predate the export).
    """
    files = []
    for p in strings_paths:
        if os.path.isdir(p):
            for name in sorted(os.listdir(p)):
                full = os.path.join(p, name)
                if os.path.isfile(full) and _strings_kind(full):
                    files.append(full)
        else:
            files.append(p)

    # Match compiled files by ESM stem prefix: shipped files use language
    # codes (Fallout4_en.STRINGS) while the CLI language argument may be a
    # full name (ENGLISH), so match <stem>_* rather than the exact spelling.
    prefix = _esm_stem(esm).lower() + "_"
    matching = [f for f in files if _strings_lang_stem(f).startswith(prefix)]
    if not matching:
        lines = ["CROSS-CHECK: no compiled %s* string files match the compile "
                 "(esm=%s language=%s); skipped" % (_esm_stem(esm), esm, language)]
        return True, lines

    by_kind = {k: {} for k in _STRINGS_KINDS}
    for path in sorted(matching):
        ids = _load_strings_ids(path)
        by_kind[_strings_kind(path)].update(ids)

    if len(matching) <= 4:
        file_list = ", ".join(os.path.basename(f) for f in sorted(matching))
    else:
        file_list = "%d files (first: %s ...)" % (
            len(matching), ", ".join(os.path.basename(f)
                                     for f in sorted(matching)[:3]))
    lines = ["CROSS-CHECK: predicted output tables vs compiled files (%s):"
             % file_list]
    ok = True
    matched = 0
    missing = []
    wrong = []
    for rec in route_records:
        sid = rec["id"]
        predicted = rec["ext"]
        present = [k for k in _STRINGS_KINDS if sid in by_kind[k]]
        if predicted in present:
            matched += 1
        elif present:
            wrong.append("%s: 0x%08X [%s] predicted %s but found in %s"
                         % (rec["source"], sid, rec["category"],
                            predicted, ",".join(present)))
        else:
            missing.append("%s: 0x%08X [%s] not in any compiled table"
                           % (rec["source"], sid, rec["category"]))

    if matched:
        lines.append("  MATCH: %d record(s) present in their predicted table" % matched)
    for item in wrong:
        ok = False
        lines.append("  MISMATCH: " + item)
    for item in missing:
        lines.append("  WARNING: " + item)
    lines.append("VERDICT: %s" % ("PASS" if ok else "FAIL"))
    return ok, lines


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
    ap.add_argument("--compile-text", nargs="*", default=None, metavar="ROOT",
                    help="validate CompileTextExport input roots (mandatory TextExport.txt, "
                         "8-hex supplemental stems, duplicate/reserved IDs, categories). "
                         "When --command is also given, the roots are taken from the command; "
                         "otherwise the positional ROOT directories are used. Fixtures: "
                         "tools/samples/compile-text/")
    ap.add_argument("--strings", nargs="*", default=None, metavar="PATH",
                    help="validate compiled localized-string files (.STRINGS/.DLSTRINGS/"
                         ".ILSTRINGS) or directories of them against the byte format from "
                         "ghidra-dump/localized-string-file-format.md: header/size invariant, "
                         "sorted ID table, offset bounds, per-kind record encoding, and the "
                         "per-language stringID partition. Combined with --compile-text, "
                         "cross-checks the predicted output tables against the compiled "
                         "files. With --text-export, the partition report also counts how "
                         "many IDs per table came from each dialog category. Real samples: "
                         ".../TEMP_BSA_FILES/*/STRINGS/")
    ap.add_argument("--text-export", nargs="*", default=None, metavar="FILE",
                    help="TextExport.txt file(s) (or directories walked for them) whose "
                         "records attribute compiled string IDs back to their source "
                         "category in the --strings partition report (dialog categories "
                         "{1,6,7,10} -> .DLSTRINGS per FUN_140916980)")
    args = ap.parse_args(argv)

    if args.strings is not None and args.compile_text is None:
        # Standalone --strings validation. When --compile-text is also given
        # the strings block is skipped: the compile-text branch runs first and
        # then cross-checks its route predictions against these files. With
        # --text-export the partition report additionally attributes every
        # compiled ID back to its TextExport category.
        if args.text_export:
            category_map, te_warnings = load_text_export_categories(args.text_export)
            for w in te_warnings:
                print("WARNING: " + w)
        else:
            category_map = None
        ok, lines = check_strings_files(args.strings, category_map)
        print("\n".join(lines))
        return 0 if ok else 1

    if args.compile_text is not None:
        # With --command also present, first lint the command line itself
        # (grammar, quoted FUN_140490960 roots), then validate the input
        # roots the command names. Without --command, the positional ROOT
        # directories are checked directly with the default ESM name.
        if args.batch_command is not None:
            ok, lines = lint_batch_command(args.batch_command)
            print("\n".join(lines))
            if not ok:
                return 1
            args2, parse_errors = _parse_compile_args(args.batch_command[args.batch_command.index(":") + 1:]
                                                      if ":" in args.batch_command else "")
            if parse_errors:
                print("\n".join("ERROR: " + e for e in parse_errors))
                return 1
            esm = args2[0]
            language = args2[1] if len(args2) > 1 else None
            roots = args2[2:] if len(args2) > 2 else []
            print()
        else:
            esm = "Fallout4.esm"
            language = None
            roots = args.compile_text
        ok, lines, route_records = check_compile_text_roots(esm, roots, language)
        print("\n".join(lines))
        if args.strings is not None:
            print()
            ok2, xlines = cross_check_compile_routes(
                route_records, args.strings, esm, language)
            print("\n".join(xlines))
            ok = ok and ok2
        return 0 if ok else 1

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
