# `--scan-index` - index a whole load order

```bash
BehaviourGraphStudio.exe --scan-index "<folder>" [--cache <file>] [--no-cache] [--rebuild] [--json]
```

`--scan-index` answers "which animation, behaviour, character and project files does this load order
reach, and where does each one come from?". It builds the same index the Animations tab builds in the
window and prints it: every `.hkx` and `.hkt` the game would load, once per winning copy, with its
data-relative virtual path, the source it won from (`loose:Data`, `loose:<mod folder>` or the
archive's file name), and its kind.

`<folder>` is either a Fallout 4 `Data` folder or the root of a Mod Organizer 2 instance (the folder
that contains `mods/` and `profiles/`). An instance is layered the way MO2 and the engine layer it, so
a packed copy that a mod overrides is reported from the mod - see
[MO2 instance setup](README.md#mo2-instance-setup).

It is the headless twin of the Animations tab: both use the same discovery, the same cache and the
same precedence merge, so their counts agree.

## What it lists

| Kind | Path |
|---|---|
| `animation` | a `.hkx`/`.hkt` under an `Animations` folder |
| `behaviour` | a `.hkx`/`.hkt` under a `Behaviors` folder |
| `character` | a file under a `Characters` folder |
| `skeleton` | a file under a `CharacterAssets` folder (a skeleton, hair or another character asset) |
| `project` | a file under `Actors` with no role folder in its path |
| `other` | anything else that is still an indexed name |

Only `.hkx` and `.hkt` names are indexed at all, so `Meshes\AnimTextData` manifests and textures are
never listed. A path with no `Actors` segment still classifies by its role folder alone.

## Exit codes

| Exit | Meaning |
|---|---|
| `0` | The load order reached at least one indexed file. |
| `1` | Nothing indexed was found: the load order is empty or points nowhere. |
| `2` | Usage or configuration error: a missing folder, or a load order that could not be resolved. |

## Flags

- `--cache <file>` - read and write the index cache at `<file>` instead of the default.
- `--no-cache` - never read or write a cache; index everything now.
- `--rebuild` - ignore an existing cache, index everything now, and write a fresh cache.
- `--json` - emit one JSON report to stdout (see the
  [overview](README.md#machine-readable-output-json)).

## The cache

Without `--cache`, the index uses a per-load-order file under
`%AppData%\BehaviourGraphStudio\cache`, named for the Data folder, the profile and the modlist path,
so two load orders never overwrite each other's cache and `--cache` is only needed to place the file
somewhere else.

Staleness is decided per source, not per document. An archive's stamp is its size and last-write
time; a loose root's stamp is a count, a byte total, the newest write time and a digest over every
indexed file's relative path, size and write time. A warm run reuses the sources that did not change,
rescans only the ones that did, and merges the results again in the game's own precedence order.
Deleting an overriding mod file restores the archive entry it hid on the next run without rescanning
the archive.

## JSON summary

`--json` writes a `scan-index` report whose `summary` has one key per kind (`animation`, `behaviour`,
`project`, `character`, `skeleton`, `other`) plus:

| Key | Meaning |
|---|---|
| `entries` | Winning rows in the index. |
| `sources` | Sources in the load order: the mod roots, the Data folder and the archives. |
| `reusedSources` | Sources taken from the cache on this run. |
| `scannedSources` | Sources rescanned on this run. |
| `cacheHit` | `1` when every source was reused, `0` otherwise. |
| `elapsedMs` | Wall time of the build. |

The report's `findings` array is empty: the index reports what a load order contains, not what is
wrong with it. Progress goes to **stderr** (one line per source, plus the cache it chose), so a
redirected stdout is still one parseable document. Human mode prints one line per kind and a `done`
line with the entry count, the reused and scanned source counts and the elapsed time - a cold and a
warm run of the same load order report the same entry count.