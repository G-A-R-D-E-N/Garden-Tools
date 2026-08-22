# CKRE — Pipeline

How the CreationKit.exe project is built and queried (mirrors the lodgen/other-thread RE flow).

## Project

- Ghidra **12.1.2** at `/tmp/ghidra_12.1.2_PUBLIC/`
- Project: `/tmp/ckre-ghidra/CKRE` (Linux), program name `CreationKit.exe`
- Import: full auto-analysis, `-analysisTimeoutPerFile 10800`

```bash
cd /tmp/ghidra_12.1.2_PUBLIC
setsid nohup ./support/analyzeHeadless /tmp/ckre-ghidra CKRE \
    -import "/run/media/user/Games-Storage/SteamLibrary/steamapps/common/Fallout 4 1946160/CreationKit.exe" \
    -overwrite -analysisTimeoutPerFile 10800 > /tmp/ckre-ghidra/import.log 2>&1 < /dev/null &
```

## Querying (headless, no analysis)

```bash
cd /tmp/ghidra_12.1.2_PUBLIC
rm -f /tmp/ckre-ghidra/CKRE.lock
./support/analyzeHeadless /tmp/ckre-ghidra CKRE -process CreationKit.exe -noanalysis \
    -scriptPath <CKRE>/ghidra-dump -postScript Query.java "<args>" 2>/dev/null
```

- Must remove the stale `CKRE.lock` before each query (headless quirk).
- `Query.java` is the generic script (substring search / decompile / dump) — see `ghidra-dump/`.

## Static recon (no ghidra needed)

```bash
strings -n 6 CreationKit.exe > strings-asc.txt
strings -el -n 6 CreationKit.exe > strings-utf16.txt
python3 CKRE/tools/pe_recon.py CreationKit.exe --imports
```

RTTI class list extraction:

```bash
grep -aoE "\.\?A[UV][A-Za-z0-9_@$?:<>]{6,}" strings-asc.txt | sort -u > rtti-classes.txt
```

## Assets

| What | Where |
|---|---|
| Binary | `.../steamapps/common/Fallout 4 1946160/CreationKit.exe` |
| Config | `.../CreationKit.ini` (see 01-CREATIONKIT-INI.md) |
| Strings | `/tmp/ckre-ghidra/strings-{asc,utf16}.txt` |
| RTTI dump | `CKRE/ghidra-dump/rtti-classes.txt`, `tes-classes.txt` |
| Decompile handoff | `CKRE/ghidra-dump/` (per-target C files) |
