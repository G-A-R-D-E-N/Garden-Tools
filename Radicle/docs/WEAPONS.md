# WEAP field coverage (#549)

Oracle: xEdit `Core/wbDefinitionsFO4.pas` at commit `93cc0bc5a1251936c3c7859eee3150eda12a62d7`. The WEAP definition is lines 12877-13032. `Common` means `Core/wbDefinitionsCommon.pas`. Other bare numbers are lines in `Core/wbDefinitionsFO4.pas`. Sizes and offsets are computed from xEdit member widths. They are not yet confirmed against official masters; `WeaponReal` (run with `FO4_DATA_DIR`) is that check.

Sources: `Radicle/schema/fallout4/fields.json` (WEAP record), generated into `Radicle/include/fo4/schema/generated_scalar_fields.inc`, `generated_formlink_lists.inc`, `generated_record_constraints.inc` and `generated_lstring_fields.inc`.

## Authoring order and cardinality

Minimum follows xEdit `SetRequired` where xEdit sets it. Only OBND (Common 9089) and the KWDA list (Common 8661-8672) do. DNAM min 1 follows the PROJ, HAZD and ARMA DNAM precedent, which xEdit does not mark. No defaults are registered, so create and add of these subrecords are not claimed.

| Subrecord | Min | Max | Coupled | xEdit |
| --- | --- | --- | --- | --- |
| EDID | 0 | 1 | | 12882 |
| VMAD | 0 | 1 | | 12883 |
| OBND | 1 | 1 | | 12884, Common 9074-9091 |
| PTRN | 0 | 1 | | 12885 |
| STCP | 0 | 1 | | 12886 |
| MODL | 0 | 1 | yes | 12888, 1030-1045 |
| MODT | 0 | 1 | | 12888, Common 8918 |
| MODC | 0 | 1 | | 12888, 4266 |
| MODS | 0 | 1 | | 12888, 4250 |
| MODF | 0 | 1 | | 12888, 4260 |
| ICON | 0 | 1 | | 12889, 5256 |
| MICO | 0 | 1 | | 12890, 5257 |
| EITM | 0 | 1 | yes | 12891, Common 8486 |
| EAMT | 0 | 1 | yes | 12891, Common 8486 |
| DEST | 0 | 1 | yes | 12892, 4275 |
| DAMC | 0 | 1 | | 12892, 4288 |
| ETYP | 0 | 1 | | 12893, 4867 |
| BIDS | 0 | 1 | | 12894 |
| BAMT | 0 | 1 | | 12895 |
| YNAM | 0 | 1 | | 12896, 5261 |
| ZNAM | 0 | 1 | | 12897 |
| KSIZ | 0 | 1 | yes | 12898, Common 8661-8672 |
| KWDA | 0 | 1 | yes | 12898, Common 8661-8672 |
| DESC | 0 | 1 | | 12899, 3694 |
| INRD | 0 | 1 | | 12900 |
| APPR | 0 | 1 | | 12901, 5275 |
| OBTE | 0 | 1 | yes | 12902, 5521 |
| NNAM | 0 | 1 | | 12903 |
| MOD4 | 0 | 1 | yes | 12904-12910 |
| MO4T | 0 | 1 | | 12904-12910 |
| MO4S | 0 | 1 | | 12904-12910, 4253 |
| MO4C | 0 | 1 | | 12904-12910, 4269 |
| MO4F | 0 | 1 | | 12904-12910, 4263 |
| DNAM | 1 | 1 | | 12911 |
| FNAM | 0 | 1 | | 12995 |
| CRDT | 0 | 1 | | 13013 |
| INAM | 0 | 1 | | 13018 |
| LNAM | 0 | 1 | | 13019 |
| WAMD | 0 | 1 | | 13020 |
| WZMD | 0 | 1 | | 13021 |
| CNAM | 0 | 1 | | 13022 |
| DAMA | 0 | 1 | | 13023, Common 8454-8463 |
| FLTR | 0 | 1 | | 13024, 5274 |
| MASE | 0 | 1 | | 13025 |

FULL (12887, 3692) is not listed. The object-template combination block also writes FULL (5525), and the authoring order cannot hold one signature twice. This is the same ambiguity that keeps FULL out of the WEAP keyword ordering.

Coupled means add and remove are refused (`subrecord_authoring.cpp:editable`). OBTE is coupled because its count matches the OBTS combinations, which are not listed.

## Scalars

Embedded fields have `payload_size`, so the scalar reader returns absent for a DNAM, FNAM or CRDT of any other size, and `WeaponReal` flags that size as a failure. Whole-payload fields (MASE, MODC, MODF, MO4C, MO4F) need the subrecord to be exactly the storage width.

| Path | Subrecord | Offset | Storage | Range | xEdit |
| --- | --- | --- | --- | --- | --- |
| WEAP\BoundMinX | OBND | 0 | i16 | -32768..32767 | Common 6440, 9074 |
| WEAP\BoundMinY | OBND | 2 | i16 | -32768..32767 | Common 6441 |
| WEAP\BoundMinZ | OBND | 4 | i16 | -32768..32767 | Common 6442 |
| WEAP\BoundMaxX | OBND | 6 | i16 | -32768..32767 | Common 6440, 9082 |
| WEAP\BoundMaxY | OBND | 8 | i16 | -32768..32767 | Common 6441 |
| WEAP\BoundMaxZ | OBND | 10 | i16 | -32768..32767 | Common 6442 |
| WEAP\Speed | DNAM | 4 | f32 | f32 | 12913 |
| WEAP\ReloadSpeed | DNAM | 8 | f32 | f32 | 12914 |
| WEAP\Reach | DNAM | 12 | f32 | f32 | 12915 |
| WEAP\MinRange | DNAM | 16 | f32 | f32 | 12916 |
| WEAP\MaxRange | DNAM | 20 | f32 | f32 | 12917 |
| WEAP\AttackDelay | DNAM | 24 | f32 | f32 | 12918 |
| WEAP\Unused | DNAM | 28 | f32 | f32 | 12919 |
| WEAP\OutOfRangeDamageMult | DNAM | 32 | f32 | f32 | 12920 |
| WEAP\OnHit | DNAM | 36 | u32 | 0..3 | 12921, enum 3677 |
| WEAP\Flags | DNAM | 48 | u32 | 0..4294967295 | 12924-12957 |
| WEAP\Capacity | DNAM | 52 | u16 | 0..65535 | 12958 |
| WEAP\AnimationType | DNAM | 54 | u8 | 0..11 | 12959-12972 |
| WEAP\SecondaryDamage | DNAM | 55 | f32 | f32 | 12973 |
| WEAP\Weight | DNAM | 59 | f32 | f32 | 12974 |
| WEAP\Value | DNAM | 63 | u32 | 0..4294967295 | 12975 |
| WEAP\BaseDamage | DNAM | 67 | u16 | 0..65535 | 12976 |
| WEAP\SoundLevel | DNAM | 69 | u32 | 0..4 | 12977, enum 3424 |
| WEAP\AccuracyBonus | DNAM | 105 | u8 | 0..255 | 12986 |
| WEAP\AnimationAttackSeconds | DNAM | 106 | f32 | f32 | 12987 |
| WEAP\ActionPointCost | DNAM | 112 | f32 | f32 | 12989 |
| WEAP\FullPowerSeconds | DNAM | 116 | f32 | f32 | 12990 |
| WEAP\MinPowerPerShot | DNAM | 120 | f32 | f32 | 12991 |
| WEAP\Stagger | DNAM | 124 | u32 | 0..4 | 12992, enum 5106 |
| WEAP\UnknownAfterAnimationAttackSeconds | DNAM | 110 | u16 | 0..65535 | 12988 |
| WEAP\UnknownEnd | DNAM | 128 | u32 | 0..4294967295 | 12993 |
| WEAP\AnimationFireSeconds | FNAM | 0 | f32 | f32 | 12996 |
| WEAP\RumbleLeftStrength | FNAM | 4 | f32 | f32 | 12997 |
| WEAP\RumbleRightStrength | FNAM | 8 | f32 | f32 | 12998 |
| WEAP\RumbleDuration | FNAM | 12 | f32 | f32 | 12999 |
| WEAP\AnimationReloadSeconds | FNAM | 16 | f32 | f32 | 13000 |
| WEAP\BoltAnimationSeconds | FNAM | 20 | u32 | 0..4294967295 | 13001 |
| WEAP\SightedTransitionSeconds | FNAM | 24 | f32 | f32 | 13002 |
| WEAP\ProjectileCount | FNAM | 28 | u8 | 0..255 | 13003 |
| WEAP\Pattern | FNAM | 33 | u32 | 0..3 | 13005-13010 |
| WEAP\RumblePeriodMs | FNAM | 37 | u32 | 0..4294967295 | 13011 |
| WEAP\CritDamageMult | CRDT | 0 | f32 | f32 | 13014 |
| WEAP\CritChargeBonus | CRDT | 4 | f32 | f32 | 13015 |
| WEAP\MeleeSpeed | MASE | whole | u32 | 0..4 | 13025-13031, enum 5-value |
| WEAP\ModelColorRemapIndex | MODC | whole | f32 | f32 | 4266 |
| WEAP\ModelFlags | MODF | whole | u8 | 0..255 | 4260, flags 4256-4259 |
| WEAP\FirstPersonColorRemapIndex | MO4C | whole | f32 | f32 | 4269 |
| WEAP\FirstPersonModelFlags | MO4F | whole | u8 | 0..255 | 4263 |
| WEAP\DamageAmount | DAMA | 4, stride 8 | u32 | 0..4294967295 | 13023, Common 8454-8463 |

Enum ranges are the count of xEdit enum members (`On Hit` 4, `Animation Type` 12, `Sound Level` 5, `Stagger` 5, `Pattern` 4, `Melee Speed` 5). The schema accepts only these values on write. Reads are not range-checked, so an out-of-range stored value stays readable. f32 fields use the same range as the other f32 scalars.

`Flags`, `ModelFlags` and `FirstPersonModelFlags` use the `flags` presentation, and `OnHit`, `AnimationType`, `SoundLevel`, `Stagger`, `Pattern` and `MeleeSpeed` use `enum`. Their xEdit names are in `fields.json` and in `ScalarDescriptor::names`: `Flags` 12924-12957, `ModelFlags` and `FirstPersonModelFlags` 4256-4259, `OnHit` 3677-3682, `AnimationType` 12959-12972, `SoundLevel` 3424-3430, `Stagger` 5106-5112, `Pattern` 13005-13010 and `MeleeSpeed` 13025-13031.

## FormLinks

Embedded form IDs go through the single-FormLink table with a payload size. Top-level form ID subrecords (INAM, LNAM, WAMD, WZMD, CNAM, BIDS, BAMT, ETYP, NNAM, INRD, YNAM, ZNAM, PTRN, STCP, MODS, MO4S, EITM) are covered by `Radicle/include/fo4/schema/formid_subrecords.inc`, which is keyed by signature. `APPR` (a keyword array, `Core/wbDefinitionsFO4.pas:5275`) is covered by `Radicle/src/schema/form_links.cpp:is_formid_array_signature`. `FLTR` is a string (`Core/wbDefinitionsFO4.pas:5274`), so it is not remapped.

| Path | Subrecord | Offset | Targets | xEdit |
| --- | --- | --- | --- | --- |
| WEAP\Ammo | DNAM | 0 | AMMO | 12912 |
| WEAP\Skill | DNAM | 40 | AVIF | 12922 |
| WEAP\Resist | DNAM | 44 | AVIF | 12923 |
| WEAP\SoundAttack | DNAM | 73 | SNDR | 12978 |
| WEAP\SoundAttack2D | DNAM | 77 | SNDR | 12979 |
| WEAP\SoundAttackLoop | DNAM | 81 | SNDR | 12980 |
| WEAP\SoundAttackFail | DNAM | 85 | SNDR | 12981 |
| WEAP\SoundIdle | DNAM | 89 | SNDR | 12982 |
| WEAP\SoundEquip | DNAM | 93 | SNDR | 12983 |
| WEAP\SoundUnEquip | DNAM | 97 | SNDR | 12984 |
| WEAP\SoundFastEquip | DNAM | 101 | SNDR | 12985 |
| WEAP\OverrideProjectile | FNAM | 29 | PROJ | 13004 |
| WEAP\CritEffect | CRDT | 8 | SPEL | 13016 |
| WEAP\DamageType | DAMA | 0, stride 8 | DMGT | 13023, Common 8454-8463 |

The FormLink fields above are nullable, matching xEdit's `[X, NULL]` lists, except `WEAP\DamageType`, which xEdit lists as `[DMGT]` with no NULL.

## Not covered

| xEdit | Reason |
| --- | --- |
| OBTE count (12902, 5521) | Count tied to the OBTS combinations. Authoring coupled, no scalar. |
| OBTS combination block (5499-5512, 5523-5526) | Variable-length, not typed. form IDs remapped by `Radicle/src/schema/form_links.cpp` (validated on 813 WEAP OBTS bodies). |
| DEST stage members (4275 onward, DSTD and others) | Only DEST and DAMC are listed. Stages are repeated. |
| MODT texture hashes (Common 8918) | Not a scalar. Preserved. |
| KSIZ count (12898) | Counter of the keyword list, maintained by the list editor. |
| VMAD (12883) | Only the authoring entry. Content is in `Radicle/include/fo4/schema/vmad.hpp`. |

## Version gates

WEAP DNAM, FNAM and CRDT have no version condition in xEdit, so each has one fixed size. The only version gate is the DAMA entry. xEdit adds a Curve Table form ID from form version 152 (`Common 8460`, `wbFromVersion(152, ...)`). The FormLink remap already applies this at `Radicle/src/schema/form_link_mixed.cpp:k_repeated_struct_layouts`. The DAMA rows (`WEAP\DamageType`, `WEAP\DamageAmount`) address every 8-byte entry for form version below 152 (the layout of the 131 records in the corpus). Both rows carry `"max_form_version": 152`, an exclusive bound. Form version 152+ (12-byte entries) is not addressed by these rows: the scalar row resolves no valid slot, and the FormLink row returns `subrecord_missing` on write, with the bytes unchanged.

## Typed read

DAMA is a count-free run of 8-byte entries, or 12-byte entries from form version 152. The DAMA rows address every 8-byte entry below form version 152 (see Version gates); 12-byte entries from form version 152 are left to this decoder, and the Curve Table form ID is not rowed. `Radicle/src/forms/weapon.cpp:decode_weapon_damage_types` decodes it, fails closed on a payload that is not whole entries, and returns an empty list when DAMA is absent. Its layout is xEdit-derived. `WeaponReal` (`tests/test_weapon_real.cpp`) is selected for the native-windows-real-data job and skips without FO4_DATA_DIR; no result is recorded here.

## Parity

READ is partial: scalars, FormLinks and DAMA decode. Generic scalar writes (`set_record_scalar`) now reach these fields for existing subrecords. DAMA entry one has a synthetic save and reparse test (`Radicle/tests/test_wp9_race_weap_rows.cpp`); no official-master write proof exists, so WRITE is not claimed.
