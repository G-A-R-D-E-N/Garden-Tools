#!/usr/bin/env python3
"""validate_dlodsettings.py -- compatibility oracle for LODGen settings files.

Checks a `.dlodsettings` (or legacy `.lod`) file against the exact parsing
rules of the reference engine (`LODGeneratorCMD/Program.cs`, decompiled from
`Edit Scripts/LODGenx64.exe`, 2026-08). It replays the engine's header
KEY=VALUE loop and the TAB-separated static-reference body and reports what
the engine would do -- accept, silently ignore, or abort -- with the exact
exit codes / crash behavior it would produce.

Usage:
    python3 validate_dlodsettings.py <file.dlodsettings> [--switch value ...]
        [--data <game Data dir>] [--gamemode <mode>] [--basedir <dir>]
    python3 validate_dlodsettings.py --list-keys   # reference card of all keys
    python3 validate_dlodsettings.py --dump-tree [--gamemode <mode>]
        [--remove-unseen] [--textureslist] [--reverse-atlas] [--level32]
        # mode family, which side-file readers run, and every exit code
        # reachable for the given GameMode (flags mirror the settings keys
        # that change reachability)

Sample fixtures: samples/valid-fo4-object.dlodsettings,
samples/valid-sse-terrain.dlodsettings (both should VERDICT: ACCEPT).

    --data <dir>     enable the body's model-file existence checks (the
                     engine checks PATHDATA + BSAs; this tool only checks
                     loose files under the given Data dir).
    --gamemode <mode> overlay the GAMEMODE= key, as the CLI switch does.
    --basedir <dir>  base dir for resolving relative side-file paths (the
                     engine resolves them against its own exe folder; the
                     settings file's folder is tried as a convenience).

Side-file cross-checks (run automatically when the settings file references
them, per the specs in XLODGEN_ATLAS_TEXTURES.md / LODGEN_ENGINE.md):

  TEXTUREATLASMAP   -> LODGen-AtlasMap.txt   (exit 1062 malformed line,
                        1063 duplicate key, <8 cols logged non-fatal)
  FLATTEXTURES/FLATNIF -> LODGenFlatTextures.txt (6 cols required, rest
                        optional; malformed float = crash, missing .nif
                        model = exit 404)
  TERRAINDATA       -> LODGen_Terrain_<ws>.bin (1112-byte cell records;
                       missing = exit 404 in TerrainX modes (and object-LOD
                       with removeUnseenFaces), truncated record = exit 645;
                       never read in flat modes -- GenerateFlat uses
                       landHeight/waterHeight and a hardcoded Dirt02 texture;
                       in BTR2RAW mode it is the OUTPUT .raw base name
                       (<value>_<L>x<L>.raw) and the inputs are the
                       worldspace's own terrain .btr LOD files)

Mode dispatch mirrors the engine's IsMode switch (Program.cs return chain):

  hybrid        returns before EVERY reader (even the required-key checks)
  btr2raw       returns after the key checks, before the atlas/flat readers
  btr2test      same
  flattes5/flatsse -> GenerateFlat; terrain bin never read, atlas/flat
                readers not reached
  Terrain*      -> GenerateLOD; terrain bin read (404/645), atlas/flat
                readers not reached (substring match: UnderTerrain* too)
  everything else (TES5/SSE/FO3/FO4/FNV aliases, textures/textureslist,
                ReverseAtlas, Merge, Convert) -> object family: atlas + flat
                readers reached, terrain bin only with removeUnseenFaces

Also note the flat-textures file feeds the OBJECT-LOD flat trees, not the
terrain FLAT generation (TerrainLOD.GenerateFlat uses landHeight/waterHeight
and a hardcoded Dirt02 texture).

Trailing `--switch value` pairs are validated with the engine's
CollectCmdArgs rules (malformed arg -> exit 666, duplicate -> exit 665).

Exit status: 0 = file is valid per the oracle, 1 = invalid, 2 = usage error.

Engine contract implemented (see LODGEN_ENGINE.md / XLODGEN_REVERSE_ENGINEERING.md):

Header:
  * lines split on the FIRST '=' only (maxsplit 2); a line WITHOUT '='
    (including an empty line) TERMINATES the header -- everything after is body
  * keys upper-cased (InvariantCulture); unknown keys silently ignored
  * empty values skipped (defaults retained); duplicates accepted, last wins
  * values parsed per key type with InvariantCulture rules; malformed values
    are unhandled exceptions (the header loop has no try/catch) => crash

Body:
  * every line of the file is considered; only lines containing a TAB and
    >= 10 tab-separated columns become statics (others are skipped)
  * col 1 refFlags hex, cols 2-8 floats, col 10 staticFlags hex,
    col 17 enableParent hex (if non-empty), cols 18/19 material-swap CSV,
    cols 20-26 optional transform (all 7 non-empty)
  * parse failures in the body abort with exit 768; missing LOD model files
    abort with exit 404 (only checked with --data)

Required keys after parsing: Worldspace (exit 4), PathOutput (exit 5),
TexturesListFile in textureslist mode (exit 6), PathData must exist (exit 3).
"""

import os
import re
import struct
import sys

# ---------------------------------------------------------------------------
# .NET-compatible scalar parsing (CultureInfo.InvariantCulture semantics)
# ---------------------------------------------------------------------------

INT_RE = re.compile(r"^\s*[+-]?\d+\s*$")
LONG_RE = INT_RE  # same syntax, wider range
ULONG_RE = re.compile(r"^\s*\d+\s*$")
BYTE_RE = ULONG_RE
FLOAT_RE = re.compile(
    r"^\s*[+-]?(?:(\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?|NaN|Infinity)\s*$"
)
HEX_RE = re.compile(r"^\s*[0-9a-fA-F]+\s*$")
BOOL_RE = re.compile(r"^\s*(True|False)\s*$", re.IGNORECASE)

INT32_MIN, INT32_MAX = -(2**31), 2**31 - 1
INT64_MIN, INT64_MAX = -(2**63), 2**63 - 1
UINT32_MAX = 2**32 - 1
UINT64_MAX = 2**64 - 1
FLOAT32_MAX = 3.4028234663852886e38


def _fmt(s):
    return f"'{s[:60]}'" + ("..." if len(s) > 60 else "")


def parse_int(s):
    if not INT_RE.match(s):
        return None, "not an int32 (Integer style, InvariantCulture)"
    v = int(s)
    if not (INT32_MIN <= v <= INT32_MAX):
        return None, "out of int32 range (would throw OverflowException)"
    return v, None


def parse_long(s):
    if not LONG_RE.match(s):
        return None, "not an int64"
    v = int(s)
    if not (INT64_MIN <= v <= INT64_MAX):
        return None, "out of int64 range (would throw OverflowException)"
    return v, None


def parse_ulong(s):
    if not ULONG_RE.match(s):
        return None, "not an uint64 (no sign allowed)"
    v = int(s)
    if v > UINT64_MAX:
        return None, "out of uint64 range (would throw OverflowException)"
    return v, None


def parse_byte(s):
    if not BYTE_RE.match(s):
        return None, "not a byte"
    v = int(s)
    if not (0 <= v <= 255):
        return None, "out of byte range 0..255 (would throw OverflowException)"
    return v, None


def parse_hex_uint(s):
    if not HEX_RE.match(s):
        return None, "not a hex uint32 (NumberStyles.HexNumber; no 0x prefix)"
    v = int(s, 16)
    if v > UINT32_MAX:
        return None, "out of uint32 range (would throw OverflowException)"
    return v, None


def parse_float(s):
    """NumberStyles.Float | AllowThousands, InvariantCulture. Returns (value, err)."""
    if re.match(r"^\s*[-+]?Infinity\s*$", s):
        return float(s.strip().replace("Infinity", "inf")), None  # .NET parses these
    if not FLOAT_RE.match(s):
        return None, "not a float (Float | AllowThousands, InvariantCulture)"
    # strip thousands separators the way .NET does (3-digit groups)
    cleaned = re.sub(r",(?=\d{3}(?:\D|$))", "", s)
    v = float(cleaned)
    if abs(v) > FLOAT32_MAX:
        return None, "out of float32 range (would throw OverflowException)"
    return v, None


def parse_bool(s):
    if not BOOL_RE.match(s):
        return None, "not a bool (bool.Parse accepts only True/False, case-insensitive)"
    return s.strip().lower() == "true", None


# ---------------------------------------------------------------------------
# Header key table: name -> (type, default, note)
# ---------------------------------------------------------------------------

# types:
#   str            raw string value
#   lower          lowercased, appended to a list
#   lower_data     lowercased, 'data\' prefix stripped, appended to a list
#   lower_semis    lowercased, split on ';', appended to a list
#   bool / int / long / ulong / byte / float / hex_uint
#   cellsw         two space-separated ints
#   gamemode       uppercased + alias table
#   dot_prefixed   '.' + value (SEASON, GRASSGID)
#   density        float / 100, clamped 0..1
#   path_data      must be an existing directory (checked with --check-paths)
#   path_output    directory created if missing (no error)
#   resource       added only if the file exists (no error)

KEYS = {
    "WORLDSPACE": ("str", "", "raw, untrimmed"),
    "CELLSW": ("cellsw", None, "'<x> <y>' space-split; needs >=2 parts"),
    "MAXLEVEL": ("int", "32", ""),
    "PATHDATA": ("path_data", "", "must be an existing directory or engine exits 3"),
    "PATHOUTPUT": ("path_output", "", "created if missing; required or engine exits 5"),
    "SEASON": ("dot_prefixed", "", "'.' + value"),
    "PATHINPUT": ("str", "", ""),
    "PATHGRASS": ("str", "", ""),
    "GRASSGID": ("dot_prefixed", ".cgid", "'.' + value"),
    "GRASSMAP": ("str", "", ""),
    "TEXTURESLISTFILE": ("str", "", "required in textureslist mode or engine exits 6"),
    "TEXTUREATLASMAP": ("str", "", ""),
    "FLATNIF": ("str", "", "alias of FLATTEXTURES"),
    "FLATTEXTURES": ("str", "", ""),
    "ALTTEXTURES": ("str", "", ""),
    "TERRAINDATA": ("str", "", ""),
    "RESOURCE": ("resource", "", "added only if the file exists"),
    "ISHDMESHMASK": ("lower_data", "", ""),
    "NOTHDMESHMASK": ("lower_data", "", ""),
    "PASSTHRUMESHMASK": ("lower_data", "", ""),
    "ISHDTEXTUREMASK": ("lower_data", "", ""),
    "TEXTUREDIFFUSEHD": ("lower_data", "", ""),
    "NOTHDTEXTUREMASK": ("lower_data", "", ""),
    "DOUBLESIDEDMESHMASK": ("lower_data", "", ""),
    "DOUBLESIDEDTEXTUREMASK": ("lower_data", "", ""),
    "NOTLODFLAGMESHMASK": ("lower", "", "applied to LOD4/8/16/32"),
    "NOTLODFLAGMESHMASKLOD4": ("lower", "", ""),
    "NOTLODFLAGMESHMASKLOD8": ("lower", "", ""),
    "NOTLODFLAGMESHMASKLOD16": ("lower", "", ""),
    "NOTLODFLAGMESHMASKLOD32": ("lower", "", ""),
    "NOTTANGENTSMESHMASK": ("lower", "", "applied to LOD4/8/16/32"),
    "NOTTANGENTSMESHMASKLOD4": ("lower", "", ""),
    "NOTTANGENTSMESHMASKLOD8": ("lower", "", ""),
    "NOTTANGENTSMESHMASKLOD16": ("lower", "", ""),
    "NOTTANGENTSMESHMASKLOD32": ("lower", "", ""),
    "IGNOREHIDE": ("lower_semis", "", "semicolon-split list"),
    "LODCOORDS4": ("lower_semis", "", ""),
    "LODCOORDS8": ("lower_semis", "", ""),
    "LODCOORDS16": ("lower_semis", "", ""),
    "LODCOORDS32": ("lower_semis", "", ""),
    "ATLASTOLERANCE": ("float", "0.2", ""),
    "IGNORETRANSLATION": ("lower", "", ""),
    "GAMEMODE": ("gamemode", "TES5", "alias table maps VR/Enderal/FNV variants"),
    "DONTGENERATEVERTEXCOLORS": ("bool", "False", ""),
    "DONTFIXTANGENTS": ("bool", "False", ""),
    "DONTGENERATETANGENTS": ("bool", "False", ""),
    "DONTMERGESHAPES": ("bool", "False", ""),
    "REMOVEUNSEENFACES": ("bool", "False", ""),
    "REMOVEUNSEENGRASS": ("bool", "False", ""),
    "REMOVEUNSEENZSHIFT": ("float", "0", ""),
    "IGNOREWATER": ("bool", "False", ""),
    "SPECIALWATER": ("bool", "False", ""),
    "CREATEHEIGHTMAP": ("bool", "False", ""),
    "USEHDFLAG": ("bool", "True", ""),
    "USEOPTIMIZER": ("bool", "False", ""),
    "IGNOREMATERIAL": ("bool", "False", ""),
    "ALLDOUBLESIDED": ("bool", "False", ""),
    "ALPHADOUBLESIDED": ("bool", "False", ""),
    "USEALPHATHRESHOLD": ("bool", "False", ""),
    "USEBACKLIGHTPOWER": ("bool", "False", ""),
    "USEDECALFLAG": ("bool", "False", ""),
    "DONTGROUP": ("bool", "False", ""),
    "FLATLODLEVELLODFLAG": ("int", "-1", "decremented by 1 after parse"),
    "GLOBALSCALE": ("float", "1", ""),
    "SCALEXY": ("float", "1", ""),
    "DEFAULTALPHATHRESHOLD": ("byte", "128", ""),
    "LEVEL32": ("bool", "False", ""),
    "LEVEL8": ("bool", "False", ""),
    "EXPERIMENTAL": ("bool", "False", ""),
    "REMOVEBLOCKS": ("bool", "False", ""),
    "VERBOSE": ("bool", "False", ""),
    "ALLHDLOD4": ("bool", "False", ""),
    "ALLHDLOD8": ("bool", "False", ""),
    "ALLHDLOD16": ("bool", "False", ""),
    "ALLHDLOD32": ("bool", "False", ""),
    "GENERATEVERTEXCOLORS": ("bool", "False", "applied to LOD4/8/16/32"),
    "GENERATEVERTEXCOLORSLOD4": ("bool", "False", ""),
    "GENERATEVERTEXCOLORSLOD8": ("bool", "False", ""),
    "GENERATEVERTEXCOLORSLOD16": ("bool", "False", ""),
    "GENERATEVERTEXCOLORSLOD32": ("bool", "False", ""),
    "MERGEVERTEXCOLORS": ("bool", "False", ""),
    "MERGEVERTEXCOLORSLOD4": ("bool", "False", ""),
    "MERGEVERTEXCOLORSLOD8": ("bool", "False", ""),
    "MERGEVERTEXCOLORSLOD16": ("bool", "False", ""),
    "MERGEVERTEXCOLORSLOD32": ("bool", "False", ""),
    "GENERATETANGENTS": ("bool", "False", ""),
    "GENERATETANGENTSLOD4": ("bool", "False", ""),
    "GENERATETANGENTSLOD8": ("bool", "False", ""),
    "GENERATETANGENTSLOD16": ("bool", "False", ""),
    "GENERATETANGENTSLOD32": ("bool", "False", ""),
    "FIXTANGENTS": ("bool", "False", ""),
    "FIXTANGENTSLOD4": ("bool", "False", ""),
    "FIXTANGENTSLOD8": ("bool", "False", ""),
    "FIXTANGENTSLOD16": ("bool", "False", ""),
    "FIXTANGENTSLOD32": ("bool", "False", ""),
    "LANDHEIGHT": ("float", "0", ""),
    "WATERHEIGHT": ("float", "0", ""),
    "SKIRTS": ("float", "256", ""),
    "QUALITYLOD4": ("float", "10", "lower = better/larger"),
    "QUALITYLOD8": ("float", "15", ""),
    "QUALITYLOD16": ("float", "20", ""),
    "QUALITYLOD32": ("float", "25", ""),
    "MAXVERTSLOD4": ("long", "32767", "capped at 65535 by the engine"),
    "MAXVERTSLOD8": ("long", "32767", ""),
    "MAXVERTSLOD16": ("long", "32767", ""),
    "MAXVERTSLOD32": ("long", "32767", ""),
    "WATERDELTALOD4": ("int", "-1", "-1 = disabled"),
    "WATERDELTALOD8": ("int", "-1", ""),
    "WATERDELTALOD16": ("int", "-1", ""),
    "WATERDELTALOD32": ("int", "-1", ""),
    "ZSHIFTLOD4": ("float", "0", ""),
    "ZSHIFTLOD8": ("float", "0", ""),
    "ZSHIFTLOD16": ("float", "0", ""),
    "ZSHIFTLOD32": ("float", "0", ""),
    "PROTECTCELLBORDERS": ("bool", "True", ""),
    "HIDEQUADS": ("bool", "False", ""),
    "GRASSBRIGHTNESSTOPR": ("float", "1", ""),
    "GRASSBRIGHTNESSTOPG": ("float", "1", ""),
    "GRASSBRIGHTNESSTOPB": ("float", "1", ""),
    "GRASSBRIGHTNESSBOTTOMR": ("float", "1", ""),
    "GRASSBRIGHTNESSBOTTOMG": ("float", "1", ""),
    "GRASSBRIGHTNESSBOTTOMB": ("float", "1", ""),
    "COMPLEXGRASSBRIGHTNESSTOPR": ("float", "1", ""),
    "COMPLEXGRASSBRIGHTNESSTOPG": ("float", "1", ""),
    "COMPLEXGRASSBRIGHTNESSTOPB": ("float", "1", ""),
    "COMPLEXGRASSBRIGHTNESSBOTTOMR": ("float", "1", ""),
    "COMPLEXGRASSBRIGHTNESSBOTTOMG": ("float", "1", ""),
    "COMPLEXGRASSBRIGHTNESSBOTTOMB": ("float", "1", ""),
    "CROWNBRIGHTNESSR": ("float", "1", ""),
    "CROWNBRIGHTNESSG": ("float", "1", ""),
    "CROWNBRIGHTNESSB": ("float", "1", ""),
    "TRUNKBRIGHTNESSR": ("float", "1", ""),
    "TRUNKBRIGHTNESSG": ("float", "1", ""),
    "TRUNKBRIGHTNESSB": ("float", "1", ""),
    "FLATTRUNKBRIGHTNESSR": ("float", "1", ""),
    "FLATTRUNKBRIGHTNESSG": ("float", "1", ""),
    "FLATTRUNKBRIGHTNESSB": ("float", "1", ""),
    "GRASSLARGEREFERENCE": ("bool", "False", ""),
    "IGNOREGRASSPLACEMENTBRIGHTNESS": ("bool", "False", ""),
    "IGNORECOMPLEXGRASSPLACEMENTBRIGHTNESS": ("bool", "False", ""),
    "GRASSDENSITY": ("density", "1", "float/100, clamped 0..1"),
    "GRASSMODE": ("int", "0", ""),
    "THREADS": ("int", None, "min(value, ProcessorCount)"),
    "THREADSPLIT": ("int", None, "min(value, ProcessorCount)"),
    "RESERVEMEM": ("ulong", None, ""),
    "MINX": ("int", None, ""),
    "MINY": ("int", None, ""),
    "MAXX": ("int", None, ""),
    "MAXY": ("int", None, ""),
    "LODLEVEL": ("int", None, ""),
}

GAMEMODE_ALIASES = {
    "FO4VR": "FO4", "ENDERAL": "TES5", "TES5VR": "SSE", "SSEVR": "SSE",
    "ENDERALSSE": "SSE", "ENDERALSE": "SSE", "FNV": "FO3",
    "TERRAINFO4VR": "TerrainFO4", "TERRAINENDERAL": "TerrainTES5",
    "TERRAINENDERALSE": "TerrainSSE", "TERRAINENDERALSSE": "TerrainSSE",
    "TERRAINSSEVR": "TerrainSSE", "TERRAINTES5VR": "TerrainSSE",
    "TERRAINFNV": "TerrainFO3", "UNDERTERRAINFO4VR": "UnderTerrainFO4",
    "UNDERTERRAINENDERAL": "UnderTerrainTES5",
    "UNDERTERRAINENDERALSE": "UnderTerrainSSE",
    "TERRAINENDERALTES5VR": "UnderTerrainTES5",
    "UNDERTERRAINFNV": "UnderTerrainFO3",
}


def parse_key_value(kind, value):
    """Return (ok, parsed, error). ok is None when the engine would throw."""
    if kind == "str":
        return True, value, None
    if kind == "lower":
        return True, value.lower(), None
    if kind == "lower_data":
        return True, value.lower().replace("data\\", ""), None
    if kind == "lower_semis":
        return True, [v for v in value.lower().split(";")], None
    if kind == "bool":
        v, e = parse_bool(value)
        return (None, None, e) if e else (True, v, None)
    if kind == "int":
        v, e = parse_int(value)
        return (None, None, e) if e else (True, v, None)
    if kind == "long":
        v, e = parse_long(value)
        return (None, None, e) if e else (True, v, None)
    if kind == "ulong":
        v, e = parse_ulong(value)
        return (None, None, e) if e else (True, v, None)
    if kind == "byte":
        v, e = parse_byte(value)
        return (None, None, e) if e else (True, v, None)
    if kind == "float":
        v, e = parse_float(value)
        return (None, None, e) if e else (True, v, None)
    if kind == "hex_uint":
        v, e = parse_hex_uint(value)
        return (None, None, e) if e else (True, v, None)
    if kind == "density":
        v, err = parse_float(value)
        if err:
            return None, None, err
        v = v / 100.0
        return True, max(0.0, min(1.0, v)), None  # clamped, never an error
    if kind == "cellsw":
        parts = value.split(" ")
        if len(parts) <= 1:
            return True, None, None  # silently ignored by the engine
        for p in parts[:2]:
            _, err = parse_int(p)
            if err:
                return None, None, f"CELLSW component {_fmt(p)}: {err}"
        return True, (int(parts[0]), int(parts[1])), None
    if kind == "gamemode":
        return True, value.upper(), None
    if kind == "dot_prefixed":
        return True, "." + value, None
    if kind == "path_data":
        return True, value, None
    if kind == "path_output":
        return True, value, None
    if kind == "resource":
        return True, value, None
    raise AssertionError(kind)


# ---------------------------------------------------------------------------
# CLI switches (engine CollectCmdArgs: malformed -> 666, duplicate -> 665)
# ---------------------------------------------------------------------------

def check_cli_switches(switches):
    """switches: list of raw argv strings following the settings file."""
    seen = {}
    problems = []
    i = 0
    while i < len(switches):
        arg = switches[i]
        if len(arg) < 2:
            problems.append(
                (666, f"arg {_fmt(arg)} is shorter than 2 chars: engine exits 666 "
                      "(Command line parameter error)"))
            i += 1
            continue
        key = arg[2:]
        value = ""
        if i + 1 < len(switches) and not switches[i + 1].startswith("--"):
            value = switches[i + 1]
            i += 1
        if key.lower() in seen:
            problems.append(
                (665, f"duplicate switch --{key} (already seen): engine exits 665 "
                      "(Command line duplicate parameter error)"))
        seen[key.lower()] = value
        i += 1
    return problems, seen


# ---------------------------------------------------------------------------
# Side-file readers (engine Program.cs atlas/flat readers, TerrainLOD.cs /
# TerrainData.cs world-data reader). Specs: XLODGEN_ATLAS_TEXTURES.md and
# LODGEN_ENGINE.md §1.
# ---------------------------------------------------------------------------

TERRAIN_RECORD = 4 + 4 + 4 + 4 + 4 + 1089 + 2 + 1  # 1112 bytes per cell record


def mode_family(gamemode):
    """Classify a (post-alias) GameMode the way Program.cs dispatches.

    Mirrors the engine's if/return chain in order -- each earlier branch
    returns before the readers that come after it:

      hybrid        -> returns at Program.cs:903 (before every side reader)
      btr2raw       -> returns at :978 (BTR2RAW: TerrainData= is the OUTPUT
                       .raw base name; inputs are the worldspace's own .btr
                       LOD files, see check_terrain_data)
      btr2test      -> returns at :987 (writes terrain desc, no .bin read)
      flat          -> returns at :1189 (GenerateFlat; uses Land/WaterHeight)
      terrain       -> returns at :1215 (GenerateLOD; reads the .bin);
                       substring match also catches UnderTerrain* modes
      object        -> reaches the atlas reader (:1281) and the flat-textures
                       reader (:1400), then LODApp (:1636)

    Returns one of: 'hybrid', 'btr2raw', 'btr2test', 'flat', 'terrain',
    'object'. Everything not caught above is object-family (TES5/SSE/FO3/FO4
    after aliasing, plus textures/textureslist/ReverseAtlas/Merge/Convert).
    """
    m = gamemode.lower()
    if "hybrid" in m:
        return "hybrid"
    if "btr2raw" in m:
        return "btr2raw"
    if "btr2test" in m:
        return "btr2test"
    if "flattes5" in m or "flatsse" in m:
        return "flat"
    if any(t in m for t in ("terraintes5", "terrainsse", "terrainfo3",
                             "terrainfo4")):
        return "terrain"
    return "object"


def _norm_win(p):
    """Settings files carry Windows backslashes; normalize for the host fs."""
    return p.replace("\\", os.sep)


DRIVE_RE = re.compile(r"^[A-Za-z]:[/\\]")


def resolve_side_file(value, basedir):
    """Engine rule: `if (!File.Exists(p) && !Path.IsPathRooted(p)) p = BaseDir + p;`

    A drive-letter path (`Z:\\...`) is rooted per .NET Path.IsPathRooted, so the
    engine never re-bases it. On a non-Windows host the file may still resolve
    through a wine drive mapping (Z: -> host root); try that before giving up.
    """
    v = _norm_win(value)
    if os.path.isabs(v) or os.path.exists(v):
        return v
    if DRIVE_RE.match(v):
        # rooted (drive-letter) path: engine uses it as-is; on Linux hosts
        # the standard wine mapping is Z: -> host root
        host = os.sep + v[2:]  # 'Z:\\foo' -> '/foo'
        if os.path.exists(host):
            return host
        return v
    if basedir:
        cand = os.path.join(basedir, v)
        if os.path.exists(cand):
            return cand
    return v


def check_atlas_map(value, basedir, gamemode, hd_list, problems, warnings, infos):
    """TEXTUREATLASMAP -> LODGen-AtlasMap.txt (Program.cs reader).

    Malformed line -> exit 1062; duplicate key -> exit 1063; line with <8
    tab-columns -> logged non-fatal. Not read in textureslist mode.
    """
    if not value:
        return
    path = resolve_side_file(value, basedir)
    if not os.path.exists(path):
        infos.append((None, f"atlas map {_fmt(value)}: file not found -- engine SKIPS it "
                            f"(File.Exists guard, no error)"))
        return
    if "textureslist" in gamemode.lower():
        infos.append((None, f"atlas map {_fmt(value)}: exists but engine SKIPS it in "
                            f"textureslist mode (File.Exists && !IsMode guard)"))
        return
    infos.append((None, f"atlas map {_fmt(value)} -> reading {path}"))
    try:
        with open(path, "rb") as f:
            raw = f.read()
        if raw.startswith(b"\xef\xbb\xbf"):
            raw = raw[3:]
        text = raw.decode("cp1252", "replace")
    except OSError as e:
        problems.append((None, f"atlas map {_fmt(value)}: unreadable ({e})"))
        return
    seen = set()
    # .NET StreamReader.ReadLine(): a single trailing '\n' is NOT an empty
    # line (only a real blank line is) -- match that, not split() semantics
    atlas_lines = text.split("\n")
    if atlas_lines and atlas_lines[-1] == "":
        atlas_lines.pop()
    for i, line in enumerate(atlas_lines, 1):
        line = line.rstrip("\r")
        cols = line.split("\t")
        if not cols or cols[0].startswith(";"):
            continue  # comment
        text14 = cols[0].lower()
        if len(cols) < 8:
            warnings.append((i, f"atlas: line {i} has {len(cols)} tab-columns (<8): "
                                f"engine logs 'Line {i} not enough data' and continues"))
            continue
        ok = True
        msg = ""
        try:
            a16 = text14.split("=")
            if len(a16) > 1:
                int(a16[1])  # alphaThreshold
            a17 = a16[0].split(",")
            a17[0]  # SourceDiffuse
            if len(a17) > 1:
                a17[1]  # SourceNormal
            if len(a17) > 2:
                a17[2]  # SourceGlow
            for c in (1, 2, 3, 4, 6, 7):
                int(cols[c])
            if len(cols) >= 10:
                float(cols[8])
                float(cols[9])
            cols[5]  # AtlasTexture (lowercased downstream)
        except ValueError as e:
            ok, msg = False, f"not an int/float: {e}"
        if not ok:
            problems.append((i, f"atlas: line {i} malformed ({msg}) -- engine logs "
                                f"'Error reading line {i}' and exits 1062"))
            continue
        if text14 in seen:
            problems.append((i, f"atlas: duplicate key {_fmt(text14)} -- engine logs "
                                f"'Texture name already defined in atlas file' and exits 1063"))
            continue
        seen.add(text14)
        diffuse = a17[0]
        if any(h in diffuse for h in hd_list):
            infos.append((i, f"atlas: {_fmt(diffuse)} matches ISHDTEXTUREMASK/TEXTUREDIFFUSEHD "
                             f"entry -- engine marks it HDTexture"))


def check_flat_textures(value, basedir, game_dir, data_dir, check_paths,
                        problems, warnings, infos):
    """FLATTEXTURES=/FLATNIF= -> LODGenFlatTextures.txt (Program.cs reader).

    Line with <6 tab-columns -> silently skipped; malformed float -> unhandled
    exception (CRASH, no try/catch around the flat parse); missing .nif model
    -> exit 404.
    """
    if not value:
        return
    path = resolve_side_file(value, basedir)
    if not os.path.exists(path):
        infos.append((None, f"flat textures {_fmt(value)}: file not found -- engine SKIPS it "
                            f"(File.Exists guard, no error)"))
        return
    infos.append((None, f"flat textures {_fmt(value)} -> reading {path}"))
    try:
        with open(path, "rb") as f:
            raw = f.read()
        if raw.startswith(b"\xef\xbb\xbf"):
            raw = raw[3:]
        text = raw.decode("cp1252", "replace")
    except OSError as e:
        problems.append((None, f"flat textures {_fmt(value)}: unreadable ({e})"))
        return
    # ReadLine semantics: trailing single '\n' is not an empty line
    flat_lines = text.split("\n")
    if flat_lines and flat_lines[-1] == "":
        flat_lines.pop()
    for i, line in enumerate(flat_lines, 1):
        line = line.rstrip("\r")
        cols = line.split("\t")
        if len(cols) < 6:
            continue  # engine silently skips (<6 cols)
        # cols 1-5 floats; NO try/catch -> malformed = crash
        for c in (1, 2, 3, 4, 5):
            try:
                float(cols[c])
            except ValueError:
                problems.append((i, f"flat: line {i} col {c + 1} ({_fmt(cols[c])}) not a float -- "
                                    f"engine CRASHES (unhandled exception, no try/catch)"))
        # optional float cols: 8 vertexColor, 9 colorVariance, 10 depth,
        # 11 shiftX, 12 shiftY, 13 heightVariance (all parsed the same way)
        for c in (8, 9, 10, 11, 12, 13):
            if len(cols) > c:
                try:
                    float(cols[c])
                except ValueError:
                    problems.append((i, f"flat: line {i} col {c + 1} ({_fmt(cols[c])}) not a float -- "
                                        f"engine CRASHES (unhandled exception)"))
        # col 6: models list (semicolon-split, 4 slots, missing -> slot 0)
        if len(cols) > 6:
            models = cols[6].split(";")
            for s in range(4):
                model = models[s] if s < len(models) else models[0]
                if ".nif" in model.lower():
                    mn = _norm_win(model)
                    exists = (os.path.exists(mn)
                              or (game_dir and os.path.exists(os.path.join(game_dir, mn)))
                              or (check_paths and data_dir
                                  and os.path.exists(os.path.join(data_dir, mn))))
                    if not exists:
                        # engine: first missing model logs 'file not found' and exits 404
                        problems.append((i, f"flat: model {_fmt(model)} not found -- engine logs "
                                            f"'file not found' and exits 404"))
                        break
                elif ".txt" in model.lower() and len(cols) >= 7 and os.path.exists(_norm_win(model)):
                    # normals/tangents/bitangents/vertexcolors file (CSV floats)
                    try:
                        with open(_norm_win(model), "rb") as f:
                            t = f.read().decode("cp1252", "replace")
                    except OSError:
                        continue
                    for ti, tline in enumerate(t.split("\n"), 1):
                        tc = tline.rstrip("\r").split(",")
                        if len(tc) == 3:
                            for v in tc:
                                try:
                                    float(v)
                                except ValueError:
                                    problems.append((i, f"flat: model {_fmt(model)} line {ti}: "
                                                        f"{_fmt(v)} not a float -- engine CRASHES"))


def check_terrain_data(value, basedir, gamemode, family, remove_unseen,
                       game_dir, worldspace, cellsw, problems, warnings, infos):
    """TERRAINDATA -> LODGen_Terrain_<ws>.bin (TerrainData/TerrainLOD reader).

    Record = 1112 bytes (x,y,waterHeight,landFlags,baseHeight,1089 deltas,
    int16+int8 trailer). Missing file -> exit 404 (TerrainX modes, or object
    LOD with removeUnseenFaces); truncated record -> exit 645. Not read at all
    in flat modes (GenerateFlat uses LandHeight/WaterHeight) nor in hybrid /
    btr2test families (the engine returns before it).

    In btr2raw mode the value is a completely different contract: BTR2RAW
    (Program.cs:978-985) writes `<value>_<L>x<L>.raw` (FileMode.Create -- a
    missing parent dir crashes) and reads the worldspace's own terrain .btr
    files (meshes\\terrain\\<ws>\\<ws>.4.<x>.<y>.btr) as input; missing quads
    are silently skipped (verbose-only log, no exit code).
    """
    if not value:
        return
    path = resolve_side_file(value, basedir)
    if family == "flat":
        infos.append((None, f"terrain data {_fmt(value)}: flat mode -- engine NEVER reads it "
                            f"(GenerateFlat uses LandHeight/WaterHeight only)"))
        return
    if family == "hybrid":
        infos.append((None, f"terrain data {_fmt(value)}: mode {gamemode} returns before the "
                            f"terrain-data reader -- engine never opens it here"))
        return
    if family == "btr2raw":
        # BTR2RAW (Program.cs:978-985 + BTR2RAW.cs). TerrainData= is the
        # OUTPUT base name, NOT an input file:
        #   Program.cs:980  text11 = BaseDir + value        (LITERAL concat)
        #   BTR2RAW.Generate  writes text11 + "_<L>x<L>.raw" (FileMode.Create)
        # with L = 1 + |southWestY| * 2 * samples, samples = 128/4 = 32
        #   => L = 1 + |cellsw.y| * 64   (both axes use lengthY in the name)
        # The parent dir of the RESULT must exist or FileStream throws an
        # unhandled DirectoryNotFoundException (live-verified). An absolute
        # drive-letter value gets mangled into BaseDir+value (crash).
        swx, swy = cellsw
        l = 1 + abs(swy) * 64
        base = basedir if basedir else ""
        out = base + value + f"_{l}x{l}.raw"  # engine: BaseDir + value + suffix
        # normalize the composed Windows-style path for the host parent check
        # (engine BaseDir is Windows-style too; value may carry backslashes)
        host_out = _norm_win(out)
        parent = os.path.dirname(host_out) or "."
        if not os.path.isdir(parent):
            problems.append((None, f"btr2raw: TerrainData={_fmt(value)} is the OUTPUT base: "
                                   f"engine writes {_fmt(out)} (BaseDir + value + suffix) and "
                                   f"its parent dir does not exist -- engine CRASHES "
                                   f"(FileMode.Create, unhandled DirectoryNotFoundException)"))
        else:
            infos.append((None, f"btr2raw: TerrainData={_fmt(value)} is the OUTPUT base: "
                                f"engine writes {_fmt(out)} (CellSW=({swx},{swy}) -> "
                                f"{l}x{l} samples)"))
        # inputs: meshes\terrain\<ws>\<ws>.4.<x>.<y>.btr for each quad in
        # [southWestX, |southWestX|) x [southWestY/4, |southWestY|/4), step 4.
        # Game.Mode == "fo3" is case-sensitive vs. the uppercased mode, so the
        # FO3 .nif branch never triggers -- always the .btr path.
        if game_dir and worldspace:
            missing = []
            for yy in range(swy // 4, abs(swy) // 4):
                for xx in range(swx, abs(swx), 4):
                    rel = os.path.join("meshes", "terrain", worldspace,
                                       f"{worldspace}.4.{xx}.{yy * 4}.btr")
                    if not os.path.exists(os.path.join(game_dir, rel)):
                        missing.append(rel)
            expected = len(range(swy // 4, abs(swy) // 4)) * len(range(swx, abs(swx), 4))
            if expected == 0:
                infos.append((None, f"btr2raw: CellSW=({swx},{swy}) gives an EMPTY quad range "
                                    f"(engine Parallel.For({swy // 4}, {abs(swy) // 4}) x "
                                    f"for i in [{swx}, {abs(swx)}) step 4) -- the .raw will be "
                                    f"all zeros; only negative CellSW values load input quads"))
            elif missing:
                infos.append((None, f"btr2raw: {len(missing)}/{expected} input quad .btr "
                                    f"file(s) missing under PathData -- engine silently "
                                    f"skips them (verbose-only log, no error); "
                                    f"e.g. {_fmt(missing[0])}"))
            else:
                infos.append((None, f"btr2raw: all {expected} input quad .btr file(s) "
                                    f"present under PathData"))
        elif not worldspace:
            infos.append((None, f"btr2raw: no Worldspace= -- cannot resolve input quad "
                                f".btr paths (engine would write a zeros-only .raw)"))
        return
    if family == "btr2test":
        infos.append((None, f"terrain data {_fmt(value)}: mode {gamemode} returns before the "
                            f"terrain-data reader -- engine never opens it here"))
        return
    if not os.path.exists(path):
        if family == "terrain" or remove_unseen:
            problems.append((None, f"terrain data {_fmt(value)}: file not found -- engine exits 404 "
                                   f"(\"{os.path.basename(path)} not found!\")"))
        else:
            infos.append((None, f"terrain data {_fmt(value)}: file not found -- not fatal here "
                                f"(object LOD reads it only with removeUnseenFaces)"))
        return
    size = os.path.getsize(path)
    n, rem = divmod(size, TERRAIN_RECORD)
    if rem:
        problems.append((None, f"terrain data {_fmt(value)}: {size} bytes is not a multiple of the "
                               f"{TERRAIN_RECORD}-byte record ({n} full + {rem} trailing) -- engine "
                               f"exits 645 (error processing terrain data)"))
        return
    infos.append((None, f"terrain data {_fmt(value)}: {n} cell record(s) x {TERRAIN_RECORD} bytes -- "
                        f"size OK; layout per LODGEN_ENGINE.md §1 (x,y int32, waterHeight float, "
                        f"landFlags int32, baseHeight float, 1089 int8 deltas, int16+int8 trailer)"))


# ---------------------------------------------------------------------------
# The oracle
# ---------------------------------------------------------------------------

def read_file(path):
    with open(path, "rb") as f:
        raw = f.read()
    if raw.startswith(b"\xef\xbb\xbf"):
        return raw[3:].decode("utf-8", "replace")
    return raw.decode("cp1252", "replace")


def validate(path, data_dir=None, cli_overrides=None, check_paths=False,
             basedir=None):
    problems = []       # (severity, line_no or None, message)
    warnings = []
    infos = []

    def err(line, msg):
        problems.append((line, msg))

    def warn(line, msg):
        warnings.append((line, msg))

    cli_overrides = cli_overrides or {}
    if basedir is None:
        # the engine resolves relative side files against its own exe folder;
        # the settings file's folder is tried as a convenience
        basedir = os.path.dirname(os.path.abspath(path))
    lines = read_file(path).split("\n")
    if lines and lines[-1] == "":
        lines.pop()

    # ---- header pass (replicates the engine's while loop) ------------------
    seen_keys = {}
    values = {}             # raw key -> value (last wins, empty skipped)
    header_end = len(lines)  # index of first body line
    gamemode = "TES5"
    for idx, raw in enumerate(lines):
        line = raw.rstrip("\r")
        parts = line.split("=", 1)
        if len(parts) != 2:
            # engine: break -- the header ends here, EVERYTHING after is body
            header_end = idx
            if line.strip() == "":
                infos.append((idx + 1, f"empty line terminates the header (engine: break)"))
            else:
                infos.append((idx + 1, f"line without '=' terminates the header (engine: break); "
                                       f"it becomes the first body line"))
            break
        key = parts[0].upper()
        value = parts[1]
        if key in seen_keys:
            warn(idx + 1, f"duplicate key {key} -- engine ACCEPTS it silently "
                          f"(last wins); CLI switches are the only duplicate-fatal path")
        seen_keys[key] = idx + 1
        if len(value) == 0:
            infos.append((idx + 1, f"{key}= with EMPTY value -- engine SKIPS it "
                                   f"(default '{KEYS.get(key, ('', '', ''))[1] if key in KEYS else ''}' retained)"))
            continue
        values[key] = value
        if key not in KEYS:
            warn(idx + 1, f"unknown key {key} -- engine silently IGNORES it (no default case)")
            continue
        kind, default, note = KEYS[key]
        ok, parsed, msg = parse_key_value(kind, value)
        if ok is None:
            err(idx + 1, f"{key}={_fmt(value)}: {msg} -- engine would CRASH here "
                         f"(header loop has no try/catch, unhandled exception)")
            continue
        if kind == "gamemode":
            gamemode = GAMEMODE_ALIASES.get(parsed, parsed)
            infos.append((idx + 1, f"GAMEMODE={value} -> engine mode '{gamemode}' "
                                   f"(substring-matched case-insensitively downstream)"))
        if kind == "cellsw" and parsed is None:
            warn(idx + 1, f"CELLSW={_fmt(value)}: <2 space-separated parts -- "
                          f"engine silently IGNORES it (southWest stays 0)")

    gamemode = cli_overrides.get("gamemode", gamemode).upper()
    family = mode_family(gamemode)

    # ---- required-key checks (engine exits) --------------------------------
    # hybrid returns at Program.cs:903 -- BEFORE the Worldspace/PathOutput/
    # TexturesListFile checks (lines 959/971/978); everything else falls
    # through to them (btr2raw/btr2test included -- they return after).
    reverse_atlas = "REVERSEATLAS" in gamemode.upper()
    if family != "hybrid":
        if not reverse_atlas and "WORLDSPACE" not in seen_keys:
            err(None, "no Worldspace= key and mode is not ReverseAtlas: engine exits 4 "
                      "(\"No Worldspace, set Worldspace=\")")
        if "PATHOUTPUT" not in seen_keys:
            err(None, "no PathOutput= key: engine exits 5 (\"No PathOutput, set PathOutput=\")")
        if "textureslist" in gamemode.lower() and "TEXTURESLISTFILE" not in seen_keys:
            err(None, "textureslist mode without TexturesListFile=: engine exits 6")
    if "PATHDATA" in seen_keys:
        pd = seen_keys["PATHDATA"]
        if check_paths:
            # value needs re-lookup (we stored line numbers); resolve from lines
            raw_val = lines[pd - 1].split("=", 1)[1]
            val = resolve_side_file(raw_val, basedir)
            if not os.path.isdir(val):
                err(pd, f"PathData={_fmt(raw_val)} is not an existing directory: "
                        f"engine exits 3")

    # ---- body pass (replicates the Parallel.For static parser) -------------
    # has4th pre-pass: any line (whole file) with a tab, >=17 cols, col16 non-empty
    has4th = False
    for raw in lines:
        if "\t" in raw and len(raw.split("\t")) >= 17 and raw.split("\t")[16] != "":
            has4th = True
            break

    level32 = False
    if "LEVEL32" in seen_keys:
        level32 = True  # value already validated as bool; True wins

    statics = 0
    skipped_no_tab = 0
    skipped_short = 0
    for idx in range(header_end, len(lines)):
        raw = lines[idx]
        line = raw.rstrip("\r")
        lineno = idx + 1
        if "\t" not in line:
            skipped_no_tab += 1
            continue
        cols = line.split("\t")
        if len(cols) < 10:
            skipped_short += 1
            warn(lineno, f"body line with {len(cols)} tab-columns (<10): engine SKIPS it")
            continue
        statics += 1
        # col 1: refFlags hex
        _, msg = parse_hex_uint(cols[1])
        if msg:
            err(lineno, f"body col 1 (refFlags)={_fmt(cols[1])}: {msg} -- engine exits 768")
            continue
        # cols 2-8: floats
        for c in (2, 3, 4, 5, 6, 7, 8):
            _, msg = parse_float(cols[c])
            if msg:
                err(lineno, f"body col {c}={_fmt(cols[c])}: {msg} -- engine exits 768")
        # col 10: staticFlags hex (present when >=13 cols)
        if len(cols) >= 13:
            _, msg = parse_hex_uint(cols[10])
            if msg:
                err(lineno, f"body col 10 (staticFlags)={_fmt(cols[10])}: {msg} -- engine exits 768")
        # cols 13..16: LOD model paths (lowercased; existence checked with --data)
        slots = 3
        if has4th and len(cols) >= 17:
            slots = 4
        for s in range(slots):
            if s + 13 < len(cols) and cols[s + 13]:
                model = cols[s + 13]
                if level32 and not has4th and s == 2 and model.lower().endswith(".nif"):
                    pass  # engine copies slot 3 to slot 4
                if check_paths and ".nif" in model.lower():
                    target = os.path.join(data_dir, _norm_win(model)) if data_dir else _norm_win(model)
                    if not os.path.exists(target):
                        err(lineno, f"LOD model not found: {model} -- engine exits 404")
        # col 17: enableParent hex (when >=20 cols and non-empty)
        if len(cols) >= 20 and cols[17] != "":
            _, msg = parse_hex_uint(cols[17])
            if msg:
                err(lineno, f"body col 17 (enableParent)={_fmt(cols[17])}: {msg} -- engine exits 768")
        # cols 18/19: material-swap CSV (count mismatch logged, not fatal)
        if len(cols) >= 20:
            src = [x for x in cols[18].split(",")]
            dst = [x for x in cols[19].split(",")]
            if len(src) != len(dst):
                warn(lineno, f"material swap source/target count mismatch "
                             f"({len(src)} vs {len(dst)}): engine logs and ignores")
        # cols 20..26: optional transform (all 7 non-empty -> parsed)
        if len(cols) == 27 and all(cols[c] != "" for c in range(20, 27)):
            for c in range(20, 27):
                _, msg = parse_float(cols[c])
                if msg:
                    err(lineno, f"body col {c} (transform)={_fmt(cols[c])}: {msg} -- engine exits 768")

    # ---- side-file cross-checks (engine readers; the engine only reaches
    #      the atlas/flat readers in the object family -- hybrid/btr2raw/
    #      btr2test/flat/TerrainX all return before Program.cs:1281) --------
    pd_raw = values.get("PATHDATA", "")
    game_dir = resolve_side_file(pd_raw, basedir) if pd_raw else ""
    if family == "object":
        hd_list = []
        for k in ("ISHDTEXTUREMASK", "TEXTUREDIFFUSEHD"):
            if k in values:
                hd_list.append(values[k].lower().replace("data\\", ""))
        check_atlas_map(values.get("TEXTUREATLASMAP", ""), basedir, gamemode,
                        hd_list, problems, warnings, infos)
        check_flat_textures(values.get("FLATTEXTURES", "") or values.get("FLATNIF", ""),
                            basedir, game_dir, data_dir, check_paths,
                            problems, warnings, infos)
    elif any(k in values for k in ("TEXTUREATLASMAP", "FLATTEXTURES", "FLATNIF")):
        infos.append((None, f"mode {gamemode} ({family}): engine returns before the atlas/"
                            f"flat-textures readers -- TEXTUREATLASMAP/FLATTEXTURES are "
                            f"NOT read in this mode"))
    remove_unseen = "REMOVEUNSEENFACES" in values and bool(
        values["REMOVEUNSEENFACES"].lower() == "true")
    worldspace = values.get("WORLDSPACE", "")
    sw = (0, 0)
    csw = values.get("CELLSW", "")
    parts = csw.split(" ")
    if len(parts) >= 2:
        try:
            sw = (int(parts[0]), int(parts[1]))
        except ValueError:
            sw = (0, 0)  # engine silently ignores malformed CELLSW
    check_terrain_data(values.get("TERRAINDATA", ""), basedir, gamemode, family,
                       remove_unseen, game_dir, worldspace, sw,
                       problems, warnings, infos)

    # ---- report ------------------------------------------------------------
    print(f"file: {path}")
    print(f"header: {len(seen_keys)} key(s), ends at line {header_end + 1} "
          f"({len(lines) - header_end} body line(s)); engine mode: {gamemode}")
    print(f"body: {statics} static(s) parsed, {skipped_no_tab} line(s) without tab skipped, "
          f"{skipped_short} line(s) with <10 columns skipped; has4th={has4th}, level32={level32}")
    for line, msg in infos:
        print(f"  [info]   line {line}: {msg}")
    for line, msg in warnings:
        print(f"  [warn]   line {line}: {msg}")
    for line, msg in problems:
        where = f"line {line}: " if line else ""
        print(f"  [ERROR]  {where}{msg}")

    print("-" * 70)
    if problems:
        print(f"VERDICT: REJECT -- {len(problems)} fatal problem(s); "
              f"the engine would abort before producing output")
        return 1
    print("VERDICT: ACCEPT -- the engine would parse this file without error "
          f"(mode {gamemode}, {statics} static(s))")
    return 0


# ---------------------------------------------------------------------------
# Exit-code / reader-reachability tree (--dump-tree)
# ---------------------------------------------------------------------------

# Every Game.logFile.Close(code) / Environment.Exit(code) site in the engine
# (LODGeneratorCMD/Program.cs, LODGenerator/*.cs), with the source site and
# which mode families can actually reach it. 'trigger' is what the user can
# do to hit the exit; 'site' is the code location.
#
# Families: 'all' = every mode, 'hybrid'/'btr2raw'/'btr2test'/'flat'/
# 'terrain'/'object' per mode_family(). 'body' = only modes that parse the
# body (all except hybrid/btr2raw/btr2test, which return before it).

EXIT_CODES = {
    1: {
        "trigger": "no command-line arguments ('Nothing to do')",
        "site": "Program.cs:110-112",
        "families": ["all"],
    },
    2: {
        "trigger": "input settings file not found ('No input file')",
        "site": "Program.cs:116-118",
        "families": ["all"],
    },
    3: {
        "trigger": "PathData= points to a missing directory ('No Data directory')",
        "site": "Program.cs:279-282 (header loop)",
        "families": ["all"],
    },
    4: {
        "trigger": "no Worldspace= key ('No Worldspace, set Worldspace='); skipped for ReverseAtlas",
        "site": "Program.cs:959-963",
        "families": ["btr2raw", "btr2test", "flat", "terrain", "object"],
    },
    5: {
        "trigger": "no PathOutput= key ('No PathOutput, set PathOutput=')",
        "site": "Program.cs:965-969",
        "families": ["btr2raw", "btr2test", "flat", "terrain", "object"],
    },
    6: {
        "trigger": "textureslist mode without TexturesListFile= ('No output file for textures list')",
        "site": "Program.cs:971-975",
        "families": ["btr2raw", "btr2test", "flat", "terrain", "object"],
    },
    404: {
        "trigger": "body static LOD model file not found ('file not found <model>')",
        "site": "Program.cs:1111-1118 (body parse)",
        "families": ["body"],
    },
    767: {
        "trigger": "body pre-pass parse error ('Error accessing parsing line'); only when LEVEL32 set",
        "site": "Program.cs:1015-1021 (has4th pre-pass)",
        "families": ["body"],
        "needs": "LEVEL32",
    },
    768: {
        "trigger": "body static parse failure ('Error parsing line')",
        "site": "Program.cs:1182-1187 (body parse catch)",
        "families": ["body"],
    },
    665: {
        "trigger": "duplicate CLI switch ('Command line duplicate paramter error')",
        "site": "Program.cs:86-89 (CollectCmdArgs)",
        "families": ["all"],
    },
    666: {
        "trigger": "malformed CLI arg shorter than 2 chars ('Command line paramter error')",
        "site": "Program.cs:50-57 (CollectCmdArgs)",
        "families": ["all"],
    },
    1062: {
        "trigger": "atlas line malformed ('Error reading line N from <atlas>')",
        "site": "Program.cs:1377-1386 (atlas reader)",
        "families": ["object"],
    },
    1063: {
        "trigger": "atlas duplicate key ('Texture name already defined in atlas file')",
        "site": "Program.cs:1387-1391 (atlas reader)",
        "families": ["object"],
    },
    535: {
        "trigger": "object static processing error (LODApp.ParseNif)",
        "site": "LODApp.cs:596-603",
        "families": ["object"],
    },
    536: {
        "trigger": "object static processing error (LODApp.GenerateGeometry)",
        "site": "LODApp.cs:400-407",
        "families": ["object"],
    },
    538: {
        "trigger": "object shape conversion error (ToBSSubIndexTriShape)",
        "site": "LODApp.cs:1912-1917 and 2481-2487",
        "families": ["object"],
    },
    540: {
        "trigger": "flat/object shape processing error",
        "site": "LODApp.cs:1370-1376",
        "families": ["object"],
    },
    548: {
        "trigger": "quad static processing error",
        "site": "LODApp.cs:3101-3106",
        "families": ["object"],
    },
    645: {
        "trigger": "terrain-data read error ('Error processing terrain data'); 640 instead if OOM",
        "site": "TerrainData.cs:120-129 / TerrainLOD.cs:262-270",
        "families": ["terrain"],
    },
    640: {
        "trigger": "out-of-memory while processing terrain data or unseen triangles",
        "site": "TerrainData.cs:122-127 / TerrainLOD.cs:444-450",
        "families": ["terrain"],
    },
    646: {
        "trigger": "error optimizing unseen triangles for a LOD level",
        "site": "TerrainLOD.cs:440-448 (GenerateQuad)",
        "families": ["terrain"],
    },
}

# terrain-family 404 (missing TerrainData bin) and object-family 404 (missing
# bin when removeUnseenFaces) share the code with the body-model 404; they are
# listed separately so the tree can attribute them to the right reader.
TERRAIN_BIN_404 = {
    "trigger": "TerrainData= file not found (terrain bin); object family only when RemoveUnseenFaces=True",
    "site": "Program.cs:1219-1225 (Terrain modes) / TerrainData.cs:42-47 / LODApp.cs:3225 (removeUnseenFaces)",
}

# Readers per family, in engine order, with the exit codes they can produce.
READERS = [
    {"name": "required-key checks", "site": "Program.cs:959-975",
     "families": ["btr2raw", "btr2test", "flat", "terrain", "object"],
     "exits": [4, 5, 6]},
    {"name": "body static parser", "site": "Program.cs:1013-1188",
     "families": ["body"],
     "exits": [767, 768, 404]},
    {"name": "atlas reader (TEXTUREATLASMAP -> LODGen-AtlasMap.txt)",
     "site": "Program.cs:1281-1393",
     "families": ["object"],
     "exits": [1062, 1063],
     "skip": "textureslist mode (File.Exists && !IsMode guard)"},
    {"name": "flat-textures reader (FLATTEXTURES/FLATNIF -> LODGenFlatTextures.txt)",
     "site": "Program.cs:1400-1517",
     "families": ["object"],
     "exits": [404],
     "skip": None},
    {"name": "terrain-bin reader (TERRAINDATA -> LODGen_Terrain_<ws>.bin)",
     "site": "TerrainLOD.cs:169-271 / TerrainData.cs:40-129",
     "families": ["terrain"],
     "exits": [404, 645, 640, 646]},
    {"name": "LODApp object generation", "site": "LODApp.cs (GenerateLOD/ParseNif)",
     "families": ["object"],
     "exits": [535, 536, 538, 540, 548, 404]},
]

FAMILY_NOTES = {
    "hybrid": "returns at Program.cs:903 BEFORE every reader and the required-key checks; "
              "the body is never parsed",
    "btr2raw": "returns at Program.cs:978 (BTR2RAW: TerrainData= is the OUTPUT .raw base "
               "name; inputs are the worldspace's own .btr LOD files); key checks run, "
               "body never parsed",
    "btr2test": "returns at Program.cs:987 (writes terrain desc, no .bin read); key checks run, "
                "body never parsed",
    "flat": "returns at Program.cs:1189 (GenerateFlat uses LandHeight/WaterHeight, never reads "
            "the terrain bin); atlas/flat readers not reached",
    "terrain": "returns at Program.cs:1215 (GenerateLOD reads the terrain bin); substring match "
               "also catches UnderTerrain* modes; atlas/flat readers not reached",
    "object": "falls through to the atlas reader (:1281) and flat-textures reader (:1400), then "
              "LODApp (:1636); terrain bin read only when RemoveUnseenFaces=True",
}


def _dump_tree_gamemode(gamemode, reverse_atlas, textureslist, remove_unseen,
                        level32):
    m = gamemode.upper()
    alias_chain = []
    cur = m
    seen = set()
    while cur in GAMEMODE_ALIASES and cur not in seen:
        seen.add(cur)
        alias_chain.append(cur)
        cur = GAMEMODE_ALIASES[cur]
    if alias_chain:
        alias_chain.append(cur)
    family = mode_family(cur)

    print(f"GameMode: {m}")
    if alias_chain:
        print(f"  alias chain: {' -> '.join(alias_chain)}")
    print(f"  family: {family}")
    print(f"  dispatch: {FAMILY_NOTES[family]}")
    print()

    print("Readers in engine order:")
    if family in ("hybrid", "btr2raw", "btr2test"):
        print(f"  [ ] all readers -- family '{family}' returns before them "
              f"(hybrid @903, btr2raw @978, btr2test @987)")
    else:
        for idx, r in enumerate(READERS):
            name = r["name"].split(" (")[0]
            if idx == 0:  # required-key checks
                runs = True
            elif idx == 1:  # body static parser
                runs = family in ("flat", "terrain", "object")
            elif idx in (2, 3, 5):  # atlas / flat-textures / LODApp
                runs = family == "object"
            elif idx == 4:  # terrain-bin reader (terrain family only)
                runs = family == "terrain"
            if family == "object" and idx == 4:
                continue  # printed below via the LODApp-specific line
            mark = "[x]" if runs else "[ ]"
            line = f"  {mark} {r['name']}  ({r['site']})"
            if runs and idx == 2 and textureslist:
                line += f"  [SKIPPED here: {r['skip']}]"
            if not runs:
                line += f"  -- not reached in family '{family}'"
            print(line)
        if family == "object":
            state = "(removeUnseenFaces=True)" if remove_unseen else "-- not reached (removeUnseenFaces=False)"
            mark = "[x]" if remove_unseen else "[ ]"
            print(f"  {mark} terrain-bin reader via LODApp {state}  "
                  f"(LODApp.cs:3225, TerrainData.cs:40-47)")
            if remove_unseen:
                print(f"      exits: 404 (missing bin), 645/640 (read error/OOM)")
    print()

    print("Exit codes reachable for this GameMode:")
    body_parsed = family in ("flat", "terrain", "object")
    reach = set()
    for code, info in sorted(EXIT_CODES.items()):
        fams = info["families"]
        if "all" in fams:
            reach.add(code)
        elif family in fams:
            reach.add(code)
        elif "body" in fams and body_parsed:
            reach.add(code)
    # family/flag adjustments
    if family == "hybrid":
        reach.discard(4); reach.discard(5); reach.discard(6)
    if textureslist:
        reach.discard(1062); reach.discard(1063)
    if reverse_atlas:
        reach.discard(4)
    if family == "terrain" or (family == "object" and remove_unseen):
        reach.add(404)  # missing TerrainData bin
    if not level32:
        reach.discard(767)
    for code in sorted(reach):
        info = EXIT_CODES[code]
        needs = f"  (needs {info['needs']})" if info.get("needs") else ""
        print(f"  {code:<4} {info['trigger']}{needs}")
        print(f"       site: {info['site']}")
    if family == "terrain" or (family == "object" and remove_unseen):
        print(f"  404  {TERRAIN_BIN_404['trigger']}")
        print(f"       site: {TERRAIN_BIN_404['site']}")
    print(f"  0    success (log 'Code: 0')")
    print()
    print("Unhandled-exception CRASH (no try/catch): malformed header value;")
    print("flat-textures malformed float in object family (Program.cs:1400-1517).")
    return 0


# ---------------------------------------------------------------------------
# Reference card (--list-keys)
# ---------------------------------------------------------------------------

TYPE_NAMES = {
    "str": "string",
    "lower": "string (lowered)",
    "lower_data": "string (lowered, data\\ stripped)",
    "lower_semis": "string (lowered, ';' list)",
    "bool": "bool (True/False)",
    "int": "int32",
    "long": "int64",
    "ulong": "uint64",
    "byte": "byte 0..255",
    "float": "float",
    "hex_uint": "hex uint32 (no 0x)",
    "cellsw": "'<x> <y>' int32s",
    "gamemode": "game mode",
    "dot_prefixed": "string ('.' prefix added)",
    "density": "percent (float/100, 0..1)",
    "path_data": "path (must be an existing dir)",
    "path_output": "path (created if missing)",
    "resource": "path (only if file exists)",
}

KEY_GROUPS = [
    ("Game & coordinates",
     lambda k: k in {"WORLDSPACE", "CELLSW", "MAXLEVEL", "GAMEMODE", "LODLEVEL",
                     "MINX", "MINY", "MAXX", "MAXY", "THREADS", "THREADSPLIT",
                     "RESERVEMEM"}),
    ("Paths & side files",
     lambda k: k in {"PATHDATA", "PATHOUTPUT", "PATHINPUT", "PATHGRASS", "SEASON",
                     "GRASSGID", "GRASSMAP", "TEXTURESLISTFILE", "TEXTUREATLASMAP",
                     "FLATNIF", "FLATTEXTURES", "ALTTEXTURES", "TERRAINDATA",
                     "RESOURCE"}),
    ("Terrain",
     lambda k: k in {"LANDHEIGHT", "WATERHEIGHT", "SKIRTS", "PROTECTCELLBORDERS",
                     "HIDEQUADS", "IGNOREWATER", "SPECIALWATER", "CREATEHEIGHTMAP"}
     or k.startswith(("QUALITYLOD", "MAXVERTSLOD", "WATERDELTALOD", "ZSHIFTLOD"))),
    ("Object LOD",
     lambda k: k in {"REMOVEUNSEENFACES", "REMOVEUNSEENGRASS", "REMOVEUNSEENZSHIFT",
                     "DONTGENERATEVERTEXCOLORS", "DONTFIXTANGENTS",
                     "DONTGENERATETANGENTS", "DONTMERGESHAPES", "DONTGROUP",
                     "IGNOREMATERIAL", "ALLDOUBLESIDED", "ALPHADOUBLESIDED",
                     "IGNOREHIDE", "LEVEL32", "LEVEL8", "EXPERIMENTAL", "VERBOSE",
                     "REMOVEBLOCKS", "FLATLODLEVELLODFLAG", "GLOBALSCALE", "SCALEXY"}
     or k.startswith(("GENERATEVERTEXCOLORS", "MERGEVERTEXCOLORS", "GENERATETANGENTS",
                      "FIXTANGENTS", "ALLHDLOD", "NOTLODFLAGMESHMASK",
                      "NOTTANGENTSMESHMASK", "LODCOORDS"))),
    ("Masks",
     lambda k: k in {"TEXTUREDIFFUSEHD"}
     or (k.endswith("MASK") and not k.startswith(("NOTLODFLAGMESHMASK",
                                                   "NOTTANGENTSMESHMASK")))),
    ("Textures & atlas",
     lambda k: k in {"ATLASTOLERANCE", "USEHDFLAG", "USEALPHATHRESHOLD",
                     "USEBACKLIGHTPOWER", "DEFAULTALPHATHRESHOLD", "USEDECALFLAG"}),
    ("Grass",
     lambda k: k.startswith(("GRASS", "CROWN", "TRUNK", "FLATTRUNK", "COMPLEXGRASS",
                             "IGNOREGRASS", "IGNORECOMPLEXGRASS"))),
    ("Other",
     lambda k: k in {"IGNORETRANSLATION", "USEOPTIMIZER"}),
]


def list_keys():
    """Print every accepted header key with type, default, and note."""
    print("LODGen settings keys (engine header parser, LODGeneratorCMD/Program.cs)")
    print("=" * 78)
    print("Header lines are KEY=VALUE; keys are matched case-insensitively;")
    print("empty values are skipped (default retained), duplicates are last-wins,")
    print("unknown keys are silently ignored, and a line without '=' ends the header.")
    print()
    print("Type legend:")
    for t in ("str", "lower", "lower_data", "lower_semis", "bool", "int", "long",
              "ulong", "byte", "float", "hex_uint", "cellsw", "gamemode",
              "dot_prefixed", "density", "path_data", "path_output", "resource"):
        print(f"  {t:<14} {TYPE_NAMES[t]}")
    print()

    assigned = set()
    for gname, pred in KEY_GROUPS:
        members = sorted(k for k in KEYS if pred(k) and k not in assigned)
        if not members:
            continue
        print(f"--- {gname} ---")
        for k in members:
            kind, default, note = KEYS[k]
            dflt = str(default) if default is not None else "--"
            print(f"  {k:<38} {TYPE_NAMES[kind]:<32} default {dflt:<6} {note}")
            assigned.add(k)
        print()
    leftover = sorted(set(KEYS) - assigned)
    if leftover:
        print("--- Other ---")
        for k in leftover:
            kind, default, note = KEYS[k]
            dflt = str(default) if default is not None else "--"
            print(f"  {k:<38} {TYPE_NAMES[kind]:<32} default {dflt:<6} {note}")
        print()
    print("GAMEMODE aliases: " + ", ".join(f"{a}->{b}" for a, b in sorted(GAMEMODE_ALIASES.items())))
    print()
    print("Side files: TEXTUREATLASMAP -> LODGen-AtlasMap.txt; FLATTEXTURES/FLATNIF ->")
    print("LODGenFlatTextures.txt; TERRAINDATA -> LODGen_Terrain_<ws>.bin (see the")
    print("XLODGEN_ATLAS_TEXTURES.md and LODGEN_ENGINE.md specs).")
    return 0


def dump_tree(argv):
    gamemode = "TES5"
    remove_unseen = textureslist = reverse_atlas = level32 = False
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--gamemode":
            if i + 1 >= len(argv):
                print("ERROR: --gamemode requires a value (e.g. --gamemode SSE)")
                return 1
            i += 1
            gamemode = argv[i]
        elif a == "--remove-unseen":
            remove_unseen = True
        elif a == "--textureslist":
            textureslist = True
        elif a == "--reverse-atlas":
            reverse_atlas = True
        elif a == "--level32":
            level32 = True
        else:
            print(f"ERROR: unknown --dump-tree flag: {a} (engine would exit 666)")
            return 1
        i += 1
    return _dump_tree_gamemode(gamemode, reverse_atlas, textureslist,
                               remove_unseen, level32)


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 2
    if argv[0] == "--list-keys":
        return list_keys()
    if argv[0] == "--dump-tree":
        return dump_tree(argv[1:])
    path = argv[0]
    if not os.path.exists(path):
        print(f"ERROR: no such file: {path} (engine would exit 2)")
        return 1
    data_dir = None
    check_paths = False
    gamemode = None
    basedir = None
    switches = []
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--data":
            i += 1
            data_dir = argv[i]
            check_paths = True
        elif a == "--gamemode":
            i += 1
            gamemode = argv[i].upper()
        elif a == "--basedir":
            i += 1
            basedir = argv[i]
        else:
            switches.append(a)
        i += 1
    cli_problems, seen = check_cli_switches(switches)
    for code, msg in cli_problems:
        print(f"  [ERROR]  cli: {msg}")
    if cli_problems:
        print("VERDICT: REJECT -- CLI switch parsing failed")
        return 1
    overrides = {"gamemode": gamemode} if gamemode else {}
    if "--gamemode" in seen and not gamemode:
        overrides["gamemode"] = seen["--gamemode"].upper()
    return validate(path, data_dir=data_dir, cli_overrides=overrides,
                    check_paths=check_paths, basedir=basedir)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
