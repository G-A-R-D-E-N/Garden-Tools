# Workshop / settlement building -- catalogued

RE pass on the settlement-building system: the `RE::Workshop` namespace (build-mode state, ratings,
placement), the `WorkshopMenu` UI class and its `WorkshopMenuNode` category tree, the Pipboy-side
`PipboyWorkshopMenu`/`PipboyWorkshopData` mirror, and the per-frame placement-ghost pipeline that
several "place anywhere" community patches hook into. Built from CommonLibF4 header
cross-referencing (`Workshop.h`, `WorkshopMenu.h`, `PipboyWorkshopMenu.h`, `PipboyWorkshopData.h` --
all four have real headers with `static_assert`-confirmed struct sizes, nothing here is
forward-declared-only) plus Ghidra decompiles and byte-pattern verification against 1.10.163 (OG).

Vanilla calls this feature "Workshop" internally (menu name `"WorkshopMenu"`, namespace
`RE::Workshop`) even though the player-facing term is "settlement building" -- both names refer to
the same system throughout this doc.

## Architecture

Three pieces, in the same shape as the player-controller doc's input/filter/camera split:

1. **Menu + UI state.** `WorkshopMenu` (`RE/W/WorkshopMenu.h`, confirmed `0x440` bytes) is a
   `GameMenuBase` plus four `BSTEventSink` bases (`UserEventEnabledEvent`,
   `Workshop::BuildableAreaEvent`, `PickRefUpdateEvent`, `Workshop::PlacementStatusEvent`). It owns
   the 3D item-preview pipeline (`Inventory3DManager`, `displayItemModels`, `item3DPositions[4]`),
   the include/exclude category lists, and the Scaleform FX target tree (`FXWorkshopMenu`, `0x1E8`
   bytes, ~35 named shader targets for the happiness-bar widgets alone).

2. **Build-mode state and rules.** The `RE::Workshop` namespace is the actual game-state layer: a
   `ContextData` struct rebuilt every frame from the player, a `PlacementItemData` singleton
   describing the item currently being placed, and ~50 free functions covering the seven settlement
   ratings (food/water/beds/power/safety/population/happiness), placement validation, scrapping, and
   the workshop-mode enter/exit lifecycle.

3. **Pipboy mirror.** `PipboyWorkshopMenu` (a thin `PipboySubMenu` override) and `PipboyWorkshopData`
   (a `PipboyDataGroup` that also sinks `ActorValueChangedEvent` and `Workshop::WorkshopModeEvent`)
   push the same ratings into the Pipboy's Scaleform-independent data model, keyed by
   `BSFixedStringCS` into two hash maps (`ratingUpdateFunctions`, `getValueFunctions`) rather than a
   fixed struct -- the rating set is data-driven on the Pipboy side even though the engine-side
   getters are individually named functions.

## Class reference

All four below have real CommonLibF4 headers -- confirmed struct layout, not reconstructed:

| Class | Base(s) | Size | Header |
|---|---|---|---|
| `Workshop::ContextData` | none | `0x30` | `RE/W/Workshop.h` |
| `Workshop::PlacementItemData` | none, `alignas(0x10)` | `0xC0` | `RE/W/Workshop.h` |
| `Workshop::WorkshopMenuNode` | none | `0x50` | `RE/W/Workshop.h` |
| `Workshop::ExtraData` | `BSExtraData` | `0x58` | `RE/W/Workshop.h` |
| `WorkshopMenu` | `GameMenuBase` + 4x `BSTEventSink<T>` | `0x440` | `RE/W/WorkshopMenu.h` |
| `WorkshopMenu::FXWorkshopMenu` | `BSGFxShaderFXTarget` | `0x1E8` | `RE/W/WorkshopMenu.h` |
| `WorkshopMenu::IconBG` | none | `0x38` | `RE/W/WorkshopMenu.h` |
| `PipboyWorkshopMenu` | `PipboySubMenu` | `0x18` | `RE/P/PipboyWorkshopMenu.h` |
| `PipboyWorkshopData` | `PipboyDataGroup` + 2x `BSTEventSink<T>` | `0x140` | `RE/P/PipboyWorkshopData.h` |

### `Workshop::ContextData` (`0x30` bytes) -- the per-frame placement input

```
NiPoint3       lookPos;     // 00  eye position
NiPoint3       lookDir;     // 0C  eye forward vector
float          zAngle;      // 18  yaw the ghost is rotated to
TESObjectCELL* parentCell;  // 20
TESWorldSpace* worldSpace;  // 28
```

Built fresh every frame by `ContextData::ContextData(Actor*)` (RVA `0x1f8830`) from
`Actor::GetEyeVector` on the player specifically, plus `Actor::data.angle.z` for `zAngle` and
`Actor::parentCell`/`parentCell->worldSpace` for the cell/worldspace pair. Two sibling overloads
exist: a raw-parameter ctor (`NiPoint3, NiPoint3, float, TESObjectCELL*, TESWorldSpace*`, RVA
`0x1f87e0`) and a copy ctor (RVA `0x1f88f0`).

This struct is the *entire* per-frame input to the placement-ghost pipeline (see "Per-frame
placement pipeline" below) -- nothing about the ghost's screen position comes from anywhere else.

### `Workshop::PlacementItemData` (`0xC0` bytes) -- the item currently being placed

```
ObjectRefHandle                 placementItem;             // 00
BSTArray<hknpShape*>            dropProxy;                 // 08
BSTArray<bhkNPCollisionObject*> body;                      // 20
std::byte                       pad38[0x80 - 0x38];         // 38  unmapped
float                           sinkDepth;                 // 80
float                           zOffset;                   // 84
float                           intersectTolerance;        // 88
float                           snapPointQueryRadius;      // 8C
float                           forwardCastZRot;           // 90
float                           xyBoundOverride;           // 94
float                           xBoundMinOverride;         // 98
float                           xBoundMaxOverride;         // 9C
float                           yBoundMinOverride;         // A0
float                           yBoundMaxOverride;         // A4
float                           zBoundMinOverride;         // A8
float                           zBoundMaxOverride;         // AC
std::uint32_t                   dropProxyIndex;            // B0
bool                            anythingIsGround;          // B4
bool                            clampUp;                   // B5
bool                            forwardCast;               // B6
bool                            recenter;                  // B7
bool                            ignoreSimpleIntersection;  // B8
bool                            allowUnsupportedStacking;  // B9
bool                            isSet;                     // BA
bool                            mustSnap;                  // BB
bool                            stacksWhenSnapped;         // BC
```

Singleton, reachable via `Workshop::GetCurrentPlacementItemData()`. **The `0xB4-0xBC` bool block does
NOT gate whether a red (invalid) placement commits** -- that's a common wrong assumption (see
"Placement commit gate" below); these flags only affect ghost *behavior* (snapping, stacking,
collision tolerance), not the pass/fail decision.

### `Workshop::WorkshopMenuNode` (`0x50` bytes) -- the build-category tree

```
BGSKeyword*                                  filterKeyword;         // 00
WorkshopMenuNode*                            parent;                // 08
BSTArray<std::unique_ptr<WorkshopMenuNode>>  children;              // 10
BGSConstructibleObject*                      recipe;                // 28
BGSConstructibleObject*                      sourceFormListRecipe;  // 30
TESForm*                                     form;                  // 38
std::uint32_t                                uniqueID;              // 40 (default -1)
std::uint16_t                                row;                   // 44
std::uint16_t                                column;                // 46
bool                                         selected;              // 48
```

One node per category/sub-category/recipe row in the build menu's tree (Structures > Wood > ... >
an individual `COBJ`). `Workshop::BuildWorkShopMenuNodeTree` (RVA `0x2122e0`) builds the whole tree
once; `WorkshopMenuNode::FindAndSetSelectedNode` (RVA `0x1f8b50`) and `::FindLocation` (RVA
`0x1f8c20`) do row/column lookups against it; `Workshop::GetSelectedWorkshopMenuNode` (RVA
`0x1f8cd0`) returns the currently-highlighted node for a given row.

### `WorkshopMenu` (`0x440` bytes) -- selected members

```
BSTArray<NiPoint3>                                     item3DPositions[4];        // 100
BSTArray<BSTTuple<DisplayItemModel, TESObjectREFR*>>   displayItemModels;         // 160
IconBG                                                 iconBG;                    // 178
Inventory3DManager                                     inv3DModelManager;         // 1B0
BGSListForm                                            includeList;               // 390
BGSListForm                                            excludeList;               // 3D8
long double                                            lastBudget;                // 420
std::uint16_t                                          topMenuCount;              // 428
bool                                                    inputAdjustMode;           // 42A
bool                                                    verticalAdjustment;        // 42B
bool                                                    inEditMode;                // 42E
bool                                                    electricalDevice;          // 42F
std::unique_ptr<FXWorkshopMenu>                        workshopMenuBase;          // 438
```

`MENU_NAME` = `"WorkshopMenu"`. `displayItemModels`/`item3DPositions[4]` back the four-slot 3D
preview carousel the vanilla build menu shows next to the category list.

## `RE::Workshop` free-function index (~50 functions, all RVA-confirmed 1.10.163 OG)

| Function | RVA | Role |
|---|---|---|
| `FreeBuild` | `0x1f3c00` | god-mode/free-build flag check, see "Placement commit gate" |
| `PlaceCurrentReference` | `0x20cda0` | the actual placement commit gate, see below |
| `Update` | `0x1ef4e0` | per-frame build-mode tick (14,136 bytes -- largest function in the namespace) |
| `RequestScrapPlacementItem` | `0x1f5330` | scrap-key handler: resolves highlighted ref from the placement handle, calls `WorkshopCanScrapRef` then `RequestScrapRef` |
| `WorkshopCanScrapRef` | `0x1f4a60` | scrap eligibility check |
| `ScrapAll` | `0x1ee170` | bulk scrap |
| `GetLoadedCounts` | `0x1ee3e0` | loaded-object counts for the budget |
| `GetAssignmentCategoryForActor` | `0x1ee5a0` | settler job-assignment category |
| `FindNearbyWorkshop` / `StartNearbyWorkshop` | `0x1ee670` / `0x1ee6f0` | proximity workshop detection + entry |
| `GetBudgetFraction` | `0x1ee770` | build-budget used/available ratio |
| `GetGeneratedPower` / `GetFood` / `GetWater` / `GetBeds` / `GetSafety` / `GetPopulation` / `GetHappiness` | `0x1ee7d0`-`0x1ee950` | raw resource totals, one function per stat |
| `GetHappinessRatingTarget` / `GetHappinessTarget` / `GetHappinessRating` | `0x1ee990`/`0x1ee9d0`/`0x1eea40` | happiness formula stages |
| `GetPowerRating` / `GetFoodRating` / `GetWaterRating` / `GetSafetyRating` / `GetBedsRating` | `0x1eeab0`-`0x1eec30` | the 0-100(ish) rating each stat maps to for the HUD/Pipboy bar colour |
| `IsTurret` / `IsMannequin` | `0x1eec50` / `0x1eecf0` | placed-object classification |
| `Repair` | `0x1eed60` | repair-all-damaged-objects |
| `UpdateOffline` | `0x1eef60` | offline-settlement catch-up tick |
| `UpdateItemPositions` | `0x1eef80` | resyncs `item3DPositions` |
| `GetObjectCount` | `0x1ef130` | placed-count of a given base form, for build-limit checks |
| `ToggleWorkshopMode` | `0x1ef1d0` | enter/exit build mode |
| `SetWorkshopAdjustInputMode` | `0x1ef200` | toggles vertical-adjust input mode |
| `InitSDM` / `KillSDM` | `0x1ef280` / `0x1ef2d0` | supply-distribution-map (supply lines) lifecycle |
| `UpdateForegroundReferenceJob` | `0x1f2c20` | background-thread job driving the foreground ref |
| `CopyLinkedContainerContents` | `0x1f2d20` | container-link duplication (e.g. connecting a shelf to storage) |
| `IsWorkshopItem` / `IsInventoryItem` / `IsActiveEditItem` / `IsSelectedWorkshopItem` | `0x1f2e00`-`0x1f2f30` | placed-ref classification predicates |
| `IsPowered` | `0x1f2f60` | power-grid connectivity check for one ref |
| `IsResourceObject` | `0x1f3000` | resource-producer classification |
| `DumpPowerGrids` | `0x1f30f0` | debug dump |
| `GetNavmeshLayer` | `0x1f3100` | navmesh layer a placed object occupies |
| `ContextData::ContextData` (3 overloads) | `0x1f87e0` / `0x1f8830` / `0x1f88f0` | see class reference above |
| `WorkshopMenuNode::*` (`Clear`/`AddFormList`/`RenumberWorkshopNodes`/`CheckForValidChildren`/`FindAndSetSelectedNode`/`FindLocation`) | `0x1f8940`-`0x1f8c20` | category-tree maintenance |
| `GetSelectedWorkshopMenuNode` | `0x1f8cd0` | current tree selection |
| `GetPathTextToWorkshopMenuNode` | `0x1f8dc0` | breadcrumb text for the currently drilled-down category |
| `GetBuildCount` | `0x206c80` | per-node placed-item count (for the "x/y built" label) |
| `BuildWorkShopMenuNodeTree` | `0x2122e0` | builds the whole category tree once |

`WorkshopMenu` class methods: ctor `0xbede90`, dtor `0xbef1e0`, `ProcessMessage` `0xbef5e0`,
`PreDisplay` `0xbefce0`, `ShowWorkshopMenu` `0xbefd00`, `HideWorkshopMenu` `0xbefd70`,
`AdvanceMovie` `0xbf1950`.

`PipboyWorkshopMenu`: ctor `0xba0420`, dtor `0xb97840`, `UpdateData` `0xba0450`.

Not covered by the automated vtable-slot audit (empty result for `WorkshopMenu` in that DB) --
slots identified manually by dumping the vtable and matching known overrides against
`GameMenuBase`'s layout: `ProcessMessage` is slot 3, `AdvanceMovie` is slot 4. Community build-mode
patches that need a per-frame tick or per-message interception hook these two slots; nothing else on
the vtable has been needed so far.

## Per-frame placement pipeline

This is the part that trips up anyone trying to move the placement ghost by writing to the
reference directly -- it doesn't work, and the decompile shows exactly why.

1. `JobListManager::MainRenderAEnd` (RVA `0xcbd450`, Address Library id `468825`) runs once per
   frame during the render job list. Among other things it constructs a fresh `ContextData` from the
   player (`ContextData::ContextData(Actor*)` call at RVA `0xcbd478`, confirmed by disassembly: an
   `E8` call whose target resolves to `0x1401f8830`).
2. That `ContextData` is handed to `Workshop::Update` (RVA `0x1ef4e0`), which positions the
   placement ghost at `lookPos + rotate(lookDir, zAngle) * PlacementItemTranslLS` and hands the
   result to an internal `_CenterPlacementItem` step that derives final orientation from `zAngle`
   plus the surface normal (`FromEulerAnglesZX`).
3. **`lookPos`/`lookDir`/`zAngle` are the only per-frame inputs.** Anything written to the
   placement ref's transform after this point is overwritten the same frame, because `Update` runs
   again next frame from a brand-new `ContextData` built the same way. This is why "write the ghost's
   position directly" patches produce a one-frame jitter that snaps back, and why free-camera build
   modes have to intercept the `ContextData` construction itself (its `lookPos`/`lookDir` come from
   `Actor::GetEyeVector` on the player body specifically -- a detached render camera does not change
   what this reads).

## Placement commit gate

`Workshop::PlaceCurrentReference` (RVA `0x20cda0`) is where an invalid (red) placement either
commits or gets refused. Decompiled shape:

```
cVar8 = IsGodMode();
if ((cVar8 != '\0') || (cVar8 = FreeBuild(), cVar8 != '\0')) { <bypass flag> = 1; }
```

i.e. a red placement is refused **unless** `IsGodMode()` or `Workshop::FreeBuild()` (RVA `0x1f3c00`)
returns true. This is the single call site every "commit red placements" community patch needs --
and only this one call site; `FreeBuild` itself is polled constantly elsewhere (`ScrapReference`,
`CollectScrapRewards`, `RequestScrapRef`, `GetBuildCount`, `PlaceCurrentReference`), so patching its
function *entry* instead of this one *call site* breaks every other caller. (Confirmed the hard way:
an entry-detour attempt against this fork's `REL::Trampoline` -- which has no entry-detour
primitive, only call/jmp-site rewriting -- destroyed `FreeBuild`'s prologue and froze the game on
approach to any workshop-linked object. The safe pattern is a call-site rewrite of the single `CALL
FreeBuild` at RVA `0x20d02b` inside `PlaceCurrentReference`, byte-verified before patching.)

**`PlacementItemData`'s bool block (`anythingIsGround`, `ignoreSimpleIntersection`, etc.) is not
part of this gate at all** -- confirmed by decompile. Those flags only affect ghost snapping and
collision-tolerance behavior, never the commit/refuse decision.

## Ground-snap and object-snap (data points for "place anywhere"-style patches)

> ### RETRACTED 2026-07-27 -- the ground-snap address below is WRONG, and so is the binary it came from
>
> `0x3c848d` is `CC CC CC 48 8D 05 ...` (INT3 padding) in the binary this game actually runs, not
> `0F 86`. Confirmed three ways: the Ghidra project `fwgh_proj_1.10.163_unpacked` reads INT3 there;
> a wildcarded search of that project for the surrounding instruction sequence returns **no hit
> anywhere in the image**; and PrismaCrafting's own shipped build logged it at runtime --
> `ground-snap site RVA 0x3c848d is not '0F 86' (got cc cc). Not patching` followed by
> `placement hooks installed -- freeBuild=true contextData=true groundSnap=false`
> (`PrismaCrafting.log`, 2026-07-23 00:57:30). The patch never applied in game.
>
> **Root cause: `ReverseEngineering/EXE/Fallout4.exe.unpacked.exe` is NOT the binary this game runs.**
> Its `.text` disagrees with the Ghidra project wholesale -- e.g. at `0x20d02b` the file sits
> mid-instruction on `0a 02 90 ...` where the real binary has `CALL Workshop::FreeBuild`, and the
> file's calls are routed through NOP-padded thunks in a `0x22xxxxx` region the real binary has no
> trace of. The Ghidra project is the copy that matches the runtime: every address taken from it has
> byte-verified at load. **Do not byte-scan that EXE for addresses -- use the Ghidra project.**
> The object-snap entry below came from the same bad source and is unverified for the same reason.
>
> Working replacement, verified against the Ghidra project on 2026-07-27: the floor-drop is not in a
> lambda at all, it is two calls `Workshop::Update` makes on its own `GroundInfo` --
> `ClampToGround` (`0x240360`, call site `0x1f190d`) and `CorrectZLocation` (`0x2419b0`, call sites
> `0x1f188a` and `0x1f1ccd`). All three sites are clean `E8 rel32`. Suppressing them with call-site
> hooks is both correct and safer than an opcode flip: no self-modifying code, nothing to restore.

- **Ground snap** -- ~~a conditional jump inside the workshop placement lambda at RVA `0x3c8430`
  (unnamed C++ lambda `operator()`, Address Library id `733031`); the jump itself at RVA `0x3c848d`,
  `0F 86 ff 01 00 00`, flip `0x86` -> `0x85` to disable the floor-drop.~~ Retracted, see above:
  wrong address, taken from the wrong binary, never worked.
- **Object snap** -- a `MOVSS` at RVA `0x37f469` (`F3 0F 10 35 ... 0F 28 C6 48 ...`; no function
  symbol at this address, sits inside an unnamed routine). Community reference patches disable it by
  overwriting with an 8-byte NOP rather than restoring the original `MOVSS` on toggle-off, which
  makes their "re-enable" step not actually restore the instruction -- noted here as a known fragility
  in that approach, not verified further in this pass.

## Everything closed out

- `ContextData`/`PlacementItemData`/`WorkshopMenuNode`/`WorkshopMenu`/`PipboyWorkshopMenu`/
  `PipboyWorkshopData` -- confirmed struct layout via CommonLibF4 header (all have real headers, none
  forward-declared-only).
- `WorkshopMenu` vtable slots for `ProcessMessage` (3) and `AdvanceMovie` (4) -- confirmed by manual
  vtable dump + matching against `GameMenuBase`.
- Per-frame placement pipeline (`MainRenderAEnd` -> `ContextData` ctor -> `Workshop::Update` ->
  `_CenterPlacementItem`) -- confirmed by decompile, explains the "ghost snaps back" failure mode.
- Placement commit gate (`PlaceCurrentReference` / `FreeBuild`) -- confirmed by decompile; confirmed
  the `PlacementItemData` bool block does NOT gate this.
- Ground snap -- **re-derived 2026-07-27** as `Workshop::Update`'s own `ClampToGround` (`0x240360`,
  site `0x1f190d`) and `CorrectZLocation` (`0x2419b0`, sites `0x1f188a` / `0x1f1ccd`), all three
  byte-verified `E8 rel32` against the Ghidra project. The earlier "opcode location and flip" entry
  here was wrong and is retracted above -- it was byte-pattern matched against
  `ReverseEngineering/EXE/Fallout4.exe.unpacked.exe`, which is not the binary the game runs.
- ~50-function `RE::Workshop` free-function index -- confirmed by name resolution against the
  1.10.163 symbol dump (table above).

## Pitch/roll -- closed out (2026-07-23)

`_CenterPlacementItem` (RVA `0x20aae0`, 1,458 bytes) **is** now decompiled, and it settles the
question: its signature is `(ContextData*, NiPoint3* pos, float* zRot)` and it builds orientation via
`FromEulerAnglesZX(matrix, zRot, 0)` -- **pitch is passed as a literal `0` and roll has no term at
all**. It writes back only position (`*param_2 += ...`) and Z-rotation
(`*param_3 = ... + ContextData->zAngle`). So there is genuinely no per-frame pitch/roll input to
inject, on either `ContextData` or this function -- confirmed, not assumed.

The workable injection point is therefore *after* the engine has oriented the ghost, not before:
`MainRenderAEnd` calls `Workshop::Update` at RVA `0xcbd482` (an `E8` whose target resolves to
`0x1401ef4e0`, confirmed by disassembly -- it sits 5 bytes after the `ContextData` ctor call at
`0xcbd478`). Rewriting *that* call site and applying pitch/roll to the placement ref immediately after
the original returns puts the write in the same frame and on the same thread as the engine's own
orientation pass, so it is the last word each frame rather than racing it. The engine rewrites the
base angle every frame and the offset is re-added on top, so it stays stable instead of accumulating.

This is the same reason the naive version fails: writing the ref's angle from the *main* thread (e.g.
a `WorkshopMenu::AdvanceMovie` hook or an `AddTask`) lands at a different point in the frame than
`Workshop::Update`, which then re-orients the ghost afterwards and wins.

## Not yet closed out

- Object-snap patch (RVA `0x37f469`) -- location confirmed, but the "restore on toggle-off" side
  (what the original bytes were before a NOP-patch) has not been independently re-derived here.
  Additionally noted this pass: unlike the ground-snap flip, this is an **8-byte** instruction
  rewrite, so it is *not* safe against a torn read if the render thread executes it mid-write --
  the ground-snap single-byte flip has no such hazard because both possible values are valid opcodes.
  Any future implementation should save the original 8 bytes for restore (rather than NOP-ing, as the
  community reference does) and should quiesce or accept that hazard.
- `WorkshopMenu`'s full vtable (only slots 3/4 identified; the rest of `GameMenuBase`'s ~20+ slots
  have not been individually walked for this class).
- `Workshop::Update` (RVA `0x1ef4e0`, 14,136 bytes) has only been partially decompiled (the ghost
  positioning math above) -- the rest of this, the single largest function in the namespace, is
  unexplored.

Struct layouts above are transcribed from `CommonLibF4/include/RE/W/Workshop.h`,
`RE/W/WorkshopMenu.h`, `RE/P/PipboyWorkshopMenu.h`, `RE/P/PipboyWorkshopData.h` -- re-read those
directly if this doc and the headers ever disagree, the headers win.
