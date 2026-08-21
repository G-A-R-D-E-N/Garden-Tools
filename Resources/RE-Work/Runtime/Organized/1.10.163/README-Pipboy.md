# Pip-Boy data/radio internals — decompile-verified

Companion doc to the growing decompile evidence log ([`DECOMPILE-RECORDS.md`](../DECOMPILE-RECORDS.md),
category `Pipboy`). Unlike `README-Quests.md`/`README-AI.md`, this doc isn't sourced from a bucketed
function-name category (Pip-Boy internals are scattered across several small, mostly-unnamed classes
that don't cluster under one prefix) — it exists because a real consumer project,
`Projects/Prisma/UI_Systems/PrismaPipboy/` (the "Amrel PDA" Pip-Boy-replacement device, see
`Projects/Prisma/UI_Systems/PrismaPipboy/docs/STATUS.md` — moved from `docs/Prisma/PrismaPipboy/`
2026-07-19), needed these facts and they'd only ever been captured as
paraphrased code comments in that project's `GameData.cpp`, not as primary decompile evidence here.
Re-decompiled directly against `fwgh_proj_1.10.163_unpacked` so the raw evidence has a permanent home
independent of any one consumer project.

See also: `docs/Prisma/COMMONLIB_FINDINGS.md` § "PipboyDataManager — the engine's own Pip-Boy data
model" for the *separate*, already-documented `PipboyDataManager` root-object schema (Radio/Workshop/
Quests/Perks/Special/Stats section keys) — that one backs most of the vanilla Pip-Boy's own data tree
and was decompiled in an earlier session; this doc covers the two things that tree does **not** cover.

## Confirmed: `MiscStatManager`'s backing store — a real `BSTArray`, richer than assumed

CommonLibF4 does not expose `MiscStatManager` at all (no header, no `ID::`). Decompiled both sides —
the write path (`AddStatPriv`, RVA `0x1e1660`) and the read path (`QueryStat`, RVA `0x1e1440`) — to
pin the layout precisely rather than trust a single side's assumptions.

**The global itself is a real `BSTArray<MiscStat, BSTArrayHeapAllocator>`**, not just a bare
pointer+count pair:

| Field | RVA (from image base) | Meaning |
|---|---|---|
| `_MiscStatsA` (data ptr) | `0x58DE838` | array base |
| `DAT_1458de840` (capacity) | `0x58DE840` | allocated capacity — a genuinely separate field from count, previously not distinguished |
| `DAT_1458de848` (count) | `0x58DE848` | live element count |

**Each `MiscStat` record is 0x20 (32) bytes** — and has more fields than any current consumer reads:

| Offset | Type | Written from (`AddStatPriv` param) | Notes |
|---|---|---|---|
| `+0x00` | `BSFixedString` | `param_2` | the stat's name — the only field `PrismaPipboy::GameData.cpp::PushStats` currently reads besides the value |
| `+0x08` | `BSFixedString` | `param_3` (via `BSFixedString::operator<<`) | a **second string field**, not currently read by any known consumer — likely a category/group label given it's built via stream-append rather than direct assignment |
| `+0x10` | `int32` | `param_4` | the stat's numeric value (what `QueryStat` returns, what `PrismaPipboy` reads) |
| `+0x14` | `int32` | `param_1` (the `MISC_STAT_T` enum passed to `AddStatPriv`) | the stat's own type/category code — **not currently surfaced anywhere** |
| `+0x18` | `BSFixedString` | `param_5` | a **third string field** — not currently read |

`AddStatPriv` itself: linear-scans the existing array for a name match; if found and the caller passed
`update=true` (`param_6`), overwrites just the value field in place and returns; if no match, appends a
new full record (all 5 fields) via `BSTArrayBase::AddUninitialized` at stride `0x20`. Gated by a global
init flag (`bMiscStatsInitialized`, checked in `QueryStat`, not shown gating `AddStatPriv` itself).

`QueryStat`: linear scan, returns the value of the first name match (writes the current entry's value
into the out-param every iteration, but returns as soon as a match sets the loop-exit flag, so the
final written value is always the matched entry's — not a bug, just an eagerly-overwritten out-param
pattern).

**Practical implication:** `PrismaPipboy`'s STATS screen currently surfaces only 2 of these 5 fields
(name @ `+0x00`, value @ `+0x10`). The unread category string (`+0x08`), type code (`+0x14`), and
third string (`+0x18`) are real, populated data — worth revisiting if the STATS screen ever wants
grouping/categorization instead of a flat list.

## Confirmed: `PipboyRadioController::SetRadioStation` (RVA `0xc3f2c0`)

The real engine call behind tuning the in-Pip-Boy radio (already correctly cited by RVA in
`PrismaPipboy::GameData.cpp`, but not previously decompiled anywhere in this workspace). Full
mechanism:

- Clamps the requested frequency into `[BGSAudio::QPipboyRadioMinFrequency, ...MaxFrequency]`.
- No-ops if the clamped frequency equals the radio's current frequency (`RadioManager::
  QCurrentPlayerFreq`).
- Otherwise calls `RadioManager::TunePlayerRadio(frequency, isStatic)` — the actual station change —
  then pushes a normalized `0..1` tune position into an animation-graph float variable
  (`"fRadioTune"`), on **both** the player's own graph and, if the player is currently wearing a
  Pip-Boy armor addon, that addon's own graph too (resolved via `TESObjectREFR::GetPipboyObject` →
  a `BIPED_OBJECT`-indexed lookup into the equipped-armor-addon table) — this is what animates the
  in-world Pip-Boy tuning dial/needle prop, not just the audio.
- Compares the normalized tune position before and after (quantized by a granularity constant,
  `DAT_143785f90`) and, if it crossed into a different quantized bucket, plays
  `"UIPipBoyRadioStaticBurst"` — the tuning-static sound cue.

## Confirmed: `PipboyWorkshopData`'s real key names — and the guess-list's actual bug is structural, not just naming

CommonLibF4 already fully types `PipboyWorkshopData` (`workshopArray` is a `PipboyArray*` of generic
`PipboyObject*` — the same tree-node type `PipboyDataManager`'s other sections use, not a bespoke
struct). What wasn't known was which **string keys** the engine actually populates on each workshop's
object. Found via the same "decompile the key-name static initializers" technique already used for
`PipboyDataManager`'s root schema (`docs/Prisma/COMMONLIB_FINDINGS.md`) — `PipboyWorkshopData` has its
own seven key-name globals, decompiled in full:

| Static | Real string value |
|---|---|
| `MapMarkerIDKeySC` | `"mapMarkerID"` |
| `OwnedKeySC` | `"owned"` |
| `RaiderKeySC` | `"raider"` |
| `RatingKeySC` | `"rating"` |
| `TextKeySC` | `"text"` |
| `ValueKeySC` | `"value"` |
| `WorkshopDataListKeySC` | `"workshopData"` |

**This changes the picture, not just the spelling.** `PrismaPipboy::GameData.cpp::PushWorkshops`
currently queries a flat guess-list of 11 candidate field names (`population, happiness, food, water,
power, defense, beds, rating, value, owned, raider`) directly on each workshop object. The real schema
has only **4 true flat fields** (`mapMarkerID`, `owned`, `raider`, `rating`) plus one nested list,
`workshopData` — and that list's entries use the generic `text`/`value` pair shape (a label string +
a number), the same self-describing row pattern this workspace has already seen elsewhere in the
Pip-Boy data tree. **Population/happiness/food/water/power/defense/beds are not flat keys at all** —
they're presumably rows *inside* `workshopData`, each with its own `text` label and `value` number,
discoverable at runtime without needing to guess or enumerate field names up front (read `owned`/
`raider`/`rating`/`mapMarkerID` directly, then iterate `workshopData` as a generic `{text, value}`
list, exactly like `MiscStatManager`'s stat list or `PipboyRadioData`'s station list are already read
elsewhere in this same project).

`PipboyWorkshopData::Func1` (RVA `0xc4a580`) — checked per today's "the placeholder Func1 name is
usually the destructor" pattern, and it is here too (calls `~PipboyWorkshopData` then conditionally
frees). No new information.

## Confirmed: `QUEST_TYPE` and `PipboyWorkshopData`/`QUEST_DATA::questType` are NOT actually unverified

Two "unverified in this fork" caveats repeated across `PrismaPipboy`'s docs turn out to be stale —
CommonLibF4 already has both, no decompiling needed, just reading the headers that were already there:

- **`RE::QUEST_TYPE`** (`CommonLibF4/include/RE/Q/QUEST_TYPE.h`) — a real, fully-named
  `enum class QUEST_TYPE : int32_t`: `kNone`, `kMainquest`, `kBrotherhood`, `kInstitue`,
  `kMinutemen`, `kRailroad`, `kMiscellaneous`, `kSidequests`, `kDLC01`–`kDLC03`.
- **`RE::QUEST_DATA::questType`** (`CommonLibF4/include/RE/Q/QUEST_DATA.h`) — a real `std::int8_t`
  field at offset `0x07`, castable directly to `QUEST_TYPE`.

`PrismaPipboy`'s QUESTS screen currently filters only ACTIVE/COMPLETED because its own docs claimed
this field was unverified/unavailable in this fork — it is not; `quest->data.questType` is directly
readable today with no decompile work required. Worth revisiting for a MAIN/SIDE/faction quest-log
category filter.

## Checked, not found: `MARKER_TYPE`→icon mapping

`RE::MARKER_TYPE` itself (`CommonLibF4/include/RE/M/MARKER_TYPE.h`) is already a complete, real,
91-value named enum (`kCave`, `kVault`, `kSettlement`, ... `kCountTotal`) — the int→**semantic name**
mapping is not missing. What's genuinely absent is an int→**icon asset** mapping, and a native-code
search for one (`GetMapMarkerIconType`/`GetMapMarkerIcon`/similar) found nothing — if vanilla FO4
picks a Pip-Boy map icon per `MARKER_TYPE`, that logic most likely lives in the vanilla `HUDMenu.swf`'s
own ActionScript, not in native engine code, which puts it outside this Ghidra pipeline's reach
(would need a Scaleform/SWF decompile, a different toolchain entirely). Not pursued further this pass.

## Not itself re-decompiled this pass (already-cited, already-trusted facts)

- **`TESQuest::GetActive`**'s flag bit (`QUEST_DATA::flags` bit 11 / `0x0800` = "tracked/active",
  gating the vanilla compass) — already decompiled and documented as part of the Quests work
  (`README-Quests.md`); `PrismaPipboy`'s TRACK QUEST action flips this same bit. Not re-decompiled
  here since it's a general Quest-system fact, not Pip-Boy-specific.
- `PipboyDataManager`'s section-key schema (Radio/Workshop/Quests/Perks/Special/Stats) — see
  `docs/Prisma/COMMONLIB_FINDINGS.md`, decompiled in an earlier session, not duplicated here.
