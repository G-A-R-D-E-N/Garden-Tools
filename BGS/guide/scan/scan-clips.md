# `--scan-clips` — find clips that play a missing animation

```bash
BehaviourGraphStudio.exe --scan-clips "<MO2 instance>" [--json]
```

Opens a [Mod Organizer 2 instance](README.md#mo2-instance-setup), layers the enabled mods over the
base game, and for every **behaviour** file that a mod actually loads, checks each clip generator's
animation against the merged load order. It reports any clip whose animation resolves nowhere.

This is the mode aimed at the **animation load‑time crash**: when the engine goes to bind the
animation a clip plays and that animation is not present anywhere in the load order, the load can
fault. Catching an unresolved clip here is catching that crash input before the game does.

## What it scans

Only behaviours that come from an **enabled mod** are scanned — a behaviour that exists purely in
the base game is skipped, because its clips reference base paths that are known‑good and would only
produce false positives. So this mode answers: "do the behaviours my mods add or override play any
animation that isn't there?"

Clip animation paths are resolved with the game's own rule: a clip names an authoring‑time `.hkt`
path; the scan strips the authoring root and matches the corresponding `.hkx` in the load order
(loose files win over archives, in load order). Weapon subgraph clips are resolved through the
per‑weapon search prefixes the engine derives from the race records, with a fallback across the
weapon folders present in the merged data.

## What it finds

| Result | Meaning |
|---|---|
| `MISSING CLIP ANIMATIONS` | A loaded behaviour has clips whose animation resolves nowhere. Each `clip '<name>' plays '<anim>' - resolves nowhere` is listed beneath it. |
| `UNREADABLE BEHAVIOUR` | The winning copy could not be read or parsed (bad data, or an unsupported HKX class layout). |

### Reading the output

```
profile Play; 12 active mod root(s); Data C:\MO2\Instances\Fallout 4\Stock Folder\Data
indexing mod archives for behaviours...
processed 3 archive(s); found 18 behaviour file(s) to check
  scanned 18/18 behaviour(s)...
MISSING CLIP ANIMATIONS  meshes/actors/character/behaviors/weapons/mymod.hkx  [loose:MyWeaponMod]  (2 clip(s))
    clip 'fire' plays 'Animations/MyMod/fire.hkx' - resolves nowhere
    clip 'reload' plays 'Animations/MyMod/reload.hkx' - resolves nowhere
done  18 winning behaviour file(s) checked, 1 with unresolved clip animations, 2 clip(s), 0 unreadable
```

- `[loose:MyWeaponMod]` / `[Archive.ba2]` is where the winning behaviour came from.
- Human output lists up to 20 unresolved clips per file; `--json` carries the full list in `details`.
- The fix for a `resolves nowhere` clip is to install the missing animation, or to add the generic
  `Animations/<clip>` fallback the engine looks for.

## JSON summary and findings

- `summary`: `checked` (behaviours with clips), `broken` (behaviours with at least one unresolved
  clip), `missingClips` (total unresolved clips), `unreadable`.
- Finding `kind`s: `missing-clip-animations`, `unreadable-behaviour`, plus `archive-warning`.
- Exit `1` when `missingClips > 0` or `unreadable > 0`; otherwise `0`.

## Scope

A clip resolving to a present file means the animation **exists**, not that its payload is
guaranteed decodable — an existing‑but‑malformed animation is a separate, suspected case tracked in
issue #209 (blocked on the runtime RE in #201/#207). This mode covers the common, confirmed cause:
a clip animation that is simply absent from the load order.
