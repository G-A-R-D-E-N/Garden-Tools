# 27 — PROJ DNAM / EXPL DATA / HAZD DNAM layouts ✅ resolved

Field layouts blocking typed creation for #76. Derived from the
CommonLibF4 runtime structs (`BGSProjectileData`, `BGSExplosionData`,
`BGSHazardData`) cross-checked statistically against every shipped payload:

| source | payloads surveyed |
|---|---|
| Fallout4.esm + DLCCoast + DLCRobot + DLCNukaWorld | 218 PROJ / 317 EXPL / 78 HAZD |
| ~830 third-party plugins (local corpus) | +408 PROJ |

All figures reproducible with `tools/dnam_survey.py` (built on
`tools/plugin_parse.py`).

---

## 1. PROJ `DNAM` — 93 bytes (all 218 shipped payloads, single size)

Runtime: `BGSProjectileData` (0x90). On disk every pointer narrows to a
formID u32 and the tail changes shape (see note below the table).

```text
offset  size  field                       notes
------  ----  --------------------------  ---------------------------------
0x00    4     u32 flags
0x04    4     f32 gravity
0x08    4     f32 speed                   (10000.0 on VATS bullets, ...)
0x0C    4     f32 range
0x10    4     formID LIGH   light          null = 0
0x14    4     formID LIGH   muzzle flash light
0x18    4     f32 explosionProximity
0x1C    4     f32 explosionTimer
0x20    4     formID EXPL   explosionType
0x24    4     formID SNDR   activeSoundLoop
0x28    4     f32 muzzleFlashDuration
0x2C    4     f32 fadeOutTime
0x30    4     f32 force
0x34    4     formID SNDR   countdownSound
0x38    4     formID SNDR   deactivateSound
0x3C    4     formID WEAP   defaultWeaponSource
0x40    4     f32 coneSpread
0x44    4     f32 collisionRadius
0x48    4     f32 lifetime
0x4C    4     f32 relaunchInterval
0x50    4     formID TXST   decalData      (DOST/TXST family)
0x54    4     formID COLL   collisionLayer
0x58    1     u8   tracerFrequency        0/1/4 in shipped files
0x59    3     -    uninitialised padding   heap remnants; ignore
0x5C    1     u8   (zero)                 0 in all 626 payloads surveyed
```

**Tail caveat:** the runtime struct also has a `vatsProjectile`
(`BGSProjectile*`) member, but it **does not serialize into DNAM**: across
the four masters and 408 third-party payloads, the dword at +0x58 never
resolves to a PROJ record — its low byte is the tracer frequency
(0/1/4) and the upper three bytes are reused heap (same pattern as XCLC
bytes 9-11). Writers must emit `[freq][3 zero bytes][0]`; readers must not
treat +0x58 as a formID.

---

## 2. EXPL `DATA` — 84 bytes canonical

Runtime: `BGSExplosionData` (0x70).

```text
offset  size  field                          notes
------  ----  -----------------------------  ------------------------------
0x00    4     formID LIGH   light            null = 0
0x04    4     formID SNDR   sound1
0x08    4     formID SNDR   sound2
0x0C    4     formID IPDS   impactDataSet
0x10    4     formID -      placedObject     TESBoundObject: MSTT/ACTI/STAT...
0x14    4     formID PROJ   spawnProjectile
0x18    12    f32[3] projectileVector       (x,y,z)
0x24    4     f32   projectileSpread
0x28    4     u32   projectileCount
0x2C    4     f32   force
0x30    4     f32   damage
0x34    4     f32   innerRadius
0x38    4     f32   outerRadius
0x3C    4     f32   imageSpaceRadius
0x40    4     f32   verticalOffsetMult
0x44    4     u32   flags
0x48    4     s32   soundLevel              enum (SOUND_LEVEL)
0x4C    4     f32   placedObjectFadeDelay
0x50    4     s32   staggerMagnitude        enum (STAGGER_MAGNITUDE)
```

**Short variants (18 records, all Fallout4.esm):** sizes 52/56/60/64 B occur
on `ExplosionAutomotiveCar0*Stage*`, `FakeForce*Explosion`,
`CarNukeTEST`, `MirelurkHunterSpitExplosion`, `AssaultronHeadExplosionLite`,
`DN084_ElevatorStaggerExplosion`, `DN085GeyserKnockdownExplosion`. They share
the same head (five formID slots) and vector block, then truncate the tail
inconsistently (some tails alias later fields), so per-size field maps are
not reliable. Recommendation: read them as prefix-truncated legacy blobs,
default missing fields to 0; **write canonical 84 B for new records.**

---

## 3. HAZD `DNAM` — 52 bytes (all 78 shipped payloads, single size)

Runtime: `BGSHazardData`. Field-for-field match with CommonLibF4 member
order; verified by eye against `FireHazardMolotov`,
`DN131_RadiationHazardStrong384`, et al.

```text
offset  size  field                        notes
------  ----  ---------------------------  --------------------------------
0x00    4     u32 limit
0x04    4     f32 radius
0x08    4     f32 lifetime
0x0C    4     f32 imageSpaceRadius
0x10    4     f32 targetInterval
0x14    4     u32 flags                    BGSHazardFlags: playerOnly,
                                           inheritDuration, alignToNormal,
                                           inheritRadius, dropToGround,
                                           taperEffectiveness (bits 0-5)
0x18    4     formID SPEL  spell
0x1C    4     formID LIGH  light
0x20    4     formID IPDS  impactDataSet
0x24    4     formID SNDR  sound
0x28    4     f32 fullEffectRadius
0x2C    4     f32 taperWeight
0x30    4     f32 taperCurve
```

---

## 4. FormID-offset verdicts (the wired-vs-not list)

Confirmed (typed resolution over all shipped payloads, plus structural fit):

| slot | verdict | evidence |
|---|---|---|
| EXPL DATA +0 LIGH | **confirm** | LIGH-resolving dwords, struct fit |
| EXPL DATA +4 SNDR | **confirm** | SNDR:185 |
| EXPL DATA +8 SNDR | **confirm** | SNDR:46 (mostly null) |
| EXPL DATA +12 IPDS | **confirm** | IPDS:81 |
| EXPL DATA +16 placed object | **confirm** | MSTT/ACTI/STAT mix — bound objects |
| PROJ DNAM +16 LIGH | **confirm** | LIGH:28 non-null |
| PROJ DNAM +20 LIGH | **confirm** | LIGH:121 (muzzle flash) |
| PROJ DNAM +32 EXPL | **confirm** | EXPL:132 |
| PROJ DNAM +36 SNDR | **confirm** | SNDR:82 |
| HAZD DNAM +24 SPEL | **confirm** | SPEL:58 |

Additional slots that are safe to wire (same evidence class):
EXPL +20 PROJ; PROJ +52/+56/+60 SNDR/SNDR/WEAP; PROJ +80 TXST,
+84 COLL; HAZD +28 LIGH, +32 IPDS, +36 SNDR.

Rejected-candidate post-mortem: EXPL +52/+60 are `innerRadius` /
`imageSpaceRadius` floats — correctly rejected. PROJ +0 is `flags` —
correctly rejected. PROJ +84 (`+0x54`) **is** a formID slot (`COLL`,
collision layer) — it only looked like coincidence because every vanilla
COLL record lives in Fallout4.esm (master index 0), so it can never carry a
DLC index; wire it, just don't expect nonzero high bytes in vanilla.

Never-wire list: PROJ +88 dword (tracer frequency + heap padding, see §1);
EXPL/HAZD enum/flag dwords whose values merely resemble small formIDs.

## 5. Tooling

* `tools/plugin_parse.py` — strict GRUP-tree plugin walker (shared library).
* `tools/dnam_survey.py` — per-offset statistical survey used above;
  pass any set of plugins on argv.
