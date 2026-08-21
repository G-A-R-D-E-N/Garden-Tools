# Runtime GameSetting table — `SettingT<GameSettingCollection>`

Target: Fallout 4 Creation Kit `1.11.137.0`, image base `0x140000000`.
The table described here is **not** the 1,629-key `CreationKit.ini` descriptor
inventory. It is the runtime game-value registry behind bare names such as
`sYes`, `fAI...`, and `fActor...`.

## Type identity

The records point at vftable `0x142f97230`. The MSVC RTTI chain identifies that
vftable as:

```text
.?AV?$SettingT@VGameSettingCollection@@@@
```

The associated RTTI objects are:

| object | address |
|---|---:|
| `SettingT<GameSettingCollection>` vftable | `0x142f97230` |
| complete-object locator | `0x1435780c8` |
| type descriptor | `0x143c67ce0` |
| `GameSettingCollection` vftable (function slots begin at `+0x08`) | `0x142f971b0` |
| `GameSettingCollection` complete-object locator | `0x143577f98` |
| `GameSettingCollection` type descriptor | `0x143c67c60` |

The RTTI base graphs are:

```text
SettingT<GameSettingCollection> -> Setting
GameSettingCollection -> SettingCollectionMap<Setting> -> SettingCollection<Setting>
```

The Elric PDB does not contain the CK-only `GameSettingCollection` class, but
it confirms the shared setting family used by this build: `Setting` has
constructors for `bool`, `int`, `unsigned int`, and `float`, typed accessors
(`Setting::Bool`, `Setting::Float`, and the conversion operators), and the
`SettingCollection<Setting>` / `SettingCollectionList<Setting>` collection
classes. Thus `SettingT<GameSettingCollection>` is the CK specialization, while
Elric supplies the template/base layout and API names rather than an address
match.

## Record layout

Every reference to `0x142f97230` in `.data` begins an 0x18-byte static record:

```text
offset size  field
+0x00  8     SettingT<GameSettingCollection> vftable pointer
+0x08  8     value union: low 32 bits for b/i/u/f; pointer for s
+0x10  8     pointer to the bare game-setting name
```

The record's `+0x10` pointer is an ASCII name. Numeric defaults are stored in
place; float values are IEEE-754 binary32 bits in the low half of `+0x08`.
String defaults use `+0x08` as a pointer to the default text. The static records
are scattered among translation-unit arrays, but each array uses the same
0x18-byte stride and the `GameSettingCollection` registration gathers them into
the runtime collection.

## Byte census

A raw scan of the `.data` section for the little-endian qword
`0x142f97230`, followed by validation of the `+0x10` pointer as an ASCII string,
finds **5,144 records**:

| subset | records |
|---|---:|
| bare game-value names (no `:`) | **5,137** |
| colon-bearing UI/INI records sharing this specialization | 7 |
| total | **5,144** |

The seven colon-bearing records are the `[General]`/`[Interface]` UI entries
that were previously counted with the INI descriptor census:

```text
fPowerArmorWorkbenchMaxPowerArmorDistance:General
uPerkVaultBoyCooldownTimeMS:Interface
iObjectivesWaitTime:Interface
iQuestWaitTime:Interface
fDoorMarkerDisplayHeight:Interface
fQuestMarkerZDiffThreshold:Interface
fDoorMarkerHeightDiffLimit:Interface
```

The corrected bare-name count is therefore **5,137** (an earlier working note
said 5,136). There are no invalid name pointers and no duplicate bare names in
this scan. Bare-name type counts are:

| prefix/type | records | interpretation |
|---|---:|---|
| `f` | 3,632 | float |
| `s` | 1,098 | string pointer |
| `i` | 371 | signed integer |
| `u` | 27 | unsigned integer |
| `b` | 9 | boolean |
| **total** | **5,137** | |

The full name population is the set of all 5,137 bare strings recovered by the
scan; the prefix census above is exhaustive. Representative name families
include the large `fAI...` group (159 records beginning with `fAI`) and the
`fActor...` group (15 records beginning with `fActor`).

## Representative record map

Addresses below are record addresses, not string addresses. `raw` is the exact
64-bit value at `record+0x08`; for floats, `default` is its binary32 decode.

| name | record | type | raw/default |
|---|---:|:---:|---|
| `sYes` | `0x143bf24e0` | s | `0x0000000142fb7c78` → `"Yes"` |
| `sNo` | `0x143bf24f8` | s | `0x0000000142fb7c7c` → `"No"` |
| `sWorkshopMenuBuildText` | `0x143b88560` | s | `0x0000000142fa05a8` → `"BUILD WITH"` |
| `fFurnitureUprightAngle` | `0x143b88150` | f | `0x41200000` → `10.0` |
| `fJetpackThrustInitial` | `0x143b88268` | f | `0x43af0000` → `350.0` |
| `fAIMaxHeadTrackDistance` | `0x143bdaf20` | f | `0x43c80000` → `400.0` |
| `fAIDistanceTeammateDrawWeapon` | `0x143bdadd0` | f | `0x44fa0000` → `2000.0` |
| `fAICombatFleeScoreThreshold` | `0x143be9af8` | f | `0x41200000` → `10.0` |
| `fActorDefaultTurningSpeed` | `0x143bdb538` | f | `0x40c90fdb` → `6.2831855` |
| `fActorLuckSkillMult` | `0x143bedbd8` | f | `0x3ecccccd` → `0.4` |
| `bAutoAimBasedOnDistance` | `0x143be8da8` | b | `0` |
| `bEyeProjectionDebug` | `0x143bf8128` | b | `0` |
| `iLootJoyEpicCollectionLevelsAllowedBelow` | `0x143b87d70` | i | `5` |
| `iHoursToRespawnCell` | `0x143bd5948` | i | `72` |
| `iLockLevelMaxHard` | `0x143b98728` | i | `75` |
| `uLegendaryRarity` | `0x143b9a278` | u | `200` |
| `uQuestTargetRepathInterval` | `0x143ba31d8` | u | `5` |

These names are game settings, not `CreationKit.ini` keys: they have no
`:Section` suffix and are not accepted by `tools/validate_ckini.py` as INI
descriptors. The seven colon-bearing records are the deliberate overlap and
should not be added to `ghidra-dump/ck-settings-keys.txt` a second time.

## Reproduction recipe

The census was obtained without executing the CK:

1. Parse the PE sections of `CreationKit.exe` and map the `.data` VA range.
2. Find every 8-byte little-endian occurrence of `0x142f97230` in `.data`.
3. Interpret `record+0x10` as a VA, require it to resolve to a non-empty ASCII
   string, and decode `record+0x08` according to its first character.
4. Split bare names from the seven names containing `:` and count prefixes.

This is deliberately separate from the INI descriptor scan: the same vftable
address is a `SettingT<GameSettingCollection>` specialization in RTTI, not a
license to treat every record that points at it as an INI setting.

## Evidence

- CK RTTI: `ghidra-dump/rtti-classes.txt` contains `GameSettingCollection` and
  `Setting` class names; the addresses above come from the binary's
  vftable → complete-object-locator → type-descriptor chain.
- Elric symbols: `ghidra-dump/elric-pdb-symbols.txt`, `Setting` constructors /
  accessors and `SettingCollection<Setting>` / `SettingCollectionList<Setting>`.
- CK string inventory: `ghidra-dump/strings-asc.txt`, including the
  `GameSettings`, `Total GameSettings: %d`, and `TESGameSettings.cpp` diagnostics.
- CK binary: `CreationKit.exe` 1.11.137.0, `.data` qword census described above.
