# 03 — Findings Log

Verified facts produced by the Ghidra pipeline. Each entry: what, how verified, and where it's used.
Newest first. Keep this in sync with `Projects\Prisma\docs\COMMONLIB_FINDINGS.md` for engine-type facts.

---

## 2026-08-14 — [CONFIRMED] The OG Address Library is contiguous, so a wrong-version lookup never fails

Measured over `re-index.sqlite`. This is why `audit_hardcoded_addresses` reported confident
nonsense rather than errors, and it is the fact any future cross-version tooling has to respect.

| version | entries | ids ≤ 1,583,368 | density over that range |
|---|---|---|---|
| 1.10.163 (kOG) | 1,582,975 | 1,582,975 | **100.0%** |
| 1.11.221 (kAE) | 651,581 | 88,355 | **5.6%** |

Every id in the OG range resolves against the OG library whether or not it means anything there.
A control of 5,000 evenly-spread low ids hits the AE library **5.3%** of the time, matching the
5.6% density — so an AE-authored id landing in the AE library is not coincidence.

**Consequence, and the test it licenses.** A file's id provenance is decidable from the hit rate
of its low ids against the AE library: ~5% ⇒ authored for OG, ~100% ⇒ authored for AE. Validated
against ground truth using upstream CommonLibF4's self-labelled `REL::VariantID{ og, ae }` table
in `include/RE/IDs.h` (1,103 entries, pulled 2026-08-14):

| slot | ids | AE-hit | classified | resolves in own library |
|---|---|---|---|---|
| 0 (kOG by construction) | 1,141 | 1.3% | `og_native` ✓ | 100.0% in 1.10.163 |
| 2 (kAE by construction) | 1,129 | 100.0% | `ae_native` ✓ | 100.0% in 1.11.221 |

Implemented as `file_provenance` in `re-mcp/src/learning/provenance.ts`. Where a file's ids sit
entirely above the OG maximum the density test has no sample, and resolution asymmetry stands in.

## 2026-08-14 — [CONFIRMED] `og_ae_map.ae_rva` is skewed one entry low — bridge by `ae_rel_id` only

`IDA_Functions_OG_AE.csv`'s `AE_Addr` column comes from a different disassembly pass than the
Address Library bins and does not agree with them. `OG_Addr` does agree; only the AE side is off.

| OG id → AE id | `address_library` AE rva | `og_ae_map.ae_rva` | function |
|---|---|---|---|
| 554863 → 2230378 | `0xca4be0` | `0xca4a50` | `Actor::GetAimVector` |
| 82382 → 2277752 | `0x1877da0` | `0x1877c80` | `bhkWorld::AddPhysicsSystem` |
| 1514984 → 2277721 | `0x18763e0` | `0x18762c0` | `bhkWorld::RemoveObjects` |
| 357289 → 2277723 | `0x1876500` | `0x18763e0` | `bhkWorld::SetMotion` |

The skew is one *entry*, not a constant byte offset — note that `SetMotion`'s recorded `ae_rva`
(`0x18763e0`) is exactly `RemoveObjects`'s real address. So matching on `ae_rva` returns the
**previous** function: wrong in the worst way, since the answer is a real, plausible, adjacent name.
It produced 90 false `slot_name_mismatch` findings before it was caught.

**Rule.** Traverse the bridge in id space: `ae_rel_id` / `og_rel_id`, both now indexed. To name an
AE address, go rva → `address_library(1.11.221)` id → `og_ae_map.ae_rel_id`. Never
`WHERE ae_rva = ?`. `og_rva` remains sound and is still how the curated OG name is reached.

## 2026-08-14 — [CONFIRMED] CommonLibF4's generated ID tables are AE-native, including the vtable table

`include/RE/IDs_VTABLE.h` and its siblings are **not** OG tables, contrary to the assumption baked
into the old single-version audit. Measured on the vendored copies in the workspace:

| table | ids | AE-hit on low ids | verdict |
|---|---|---|---|
| `IDs_VTABLE.h` | 10,364 (8,637 low) | **100.0%** | `ae_native` |
| `IDs_RTTI.h` | 12,281 | 99.8% | `ae_native` |
| `IDs.h` (scalar entries) | 1,068 | 99.9% | `ae_native` |
| `IDs_NiRTTI.h` | 347 | 100.0% | `ae_native` |
| `VTABLE_IDs.h` (commonlibf4vr fork) | 9,990 | 85.6% | `ae_native` |

All six vendored copies of `IDs_VTABLE.h` in the workspace are identical on this measure
(`Actor[0]` = `REL::ID(1455516)` in every one).

These are written as **scalar** `REL::ID(x)`, which `REL/ID.h` replicates into all three runtime
slots — so on kOG and kNG they resolve into `.rdata` at an unrelated address. That is upstream's
to fix, not ours; recorded here so it is never re-derived as a local bug.

**Scope correction worth keeping.** The 2,028 "missing" and ~9,937 "disagreement" counts that
prompted this work were `Commonwealth Online/commonwealth-online`, and re-running the fixed audit
there gives **first-party references: 0** — all 25,682 are vendored CommonLibF4. None of the alarm
was about workspace code.

---

## 2026-08-13 — [CONFIRMED] MultiActivateManager::ButtonInfo layout (OG 1.10.163)

Settled for PrismaUI_F4's `VanillaUISuppressor.cpp` activate-choice capture. `search_findings` had no
prior entry for `ButtonInfo` / `MultiActivateManager`, so this is the first record.

`sizeof(ButtonInfo) == 0x20`, from `BSTArray<ButtonInfo,BSTSmallArrayHeapAllocator<128>>::operator[]`
(`0xa9df40`) and `MultiActivateManager::OnButtonRelease` (`0xb7b4d0`), both of which compute
`SHL RAX,0x5` (`index * 0x20`).

| offset | size | type | evidence |
|---|---|---|---|
| 0x00 | 0x10 | `BSStringT<char,-1,DynamicMemoryManagementPol>` label | ctor `0xb7b560` calls `BSStringT::Set(this,"",0)`; dtor `0xa9da60` frees `*(void**)this` via `MemoryManager__Deallocate` (dynamic policy, **not** `BSFixedString`) |
| 0x10 | 0x08 | `BSTSmartPointer<IMultiActivateUser,BSTSmartPointerIntrusiveRefCount>` listener | `IsPopulated` `0xb7b680` = `CMP [RCX+0x10],0; SETNZ AL`; dtor/operator= refcount it; `PopulateData` `0x8329a0` writes it via `BSTSmartPointer::operator=` |
| 0x18 | 0x04 | `BUTTON_INDEX` (u32) | `ButtonInfo(BUTTON_INDEX)` ctor `0xb7b5b0` = `*(u32*)(this+0x18)=param_1`; default ctor writes sentinel 4; `operator==` `0xb7b600` compares it |
| 0x1c | 0x01 | `bool valid` | ctors set `this[0x1c]=1`; `operator==`/`operator=` copy it as a byte; `OnButtonRelease` gates on `CMP byte ptr [RAX+RDI+0x1c],0` |

`BUTTON_INDEX` is a 4-byte enum from the 1.10.155 private symbols type record
(`/local symbols/MultiActivateManager/BUTTON_INDEX`), six entries -- 4 valid buttons plus a count
sentinel and one alias:

| name | value |
|---|---|
| `BTN_A` | 0 |
| `BTN_B` | 1 |
| `BTN_SECONDARYACTIVATE` | 1 (alias of `BTN_B`) |
| `BTN_X` | 2 |
| `BTN_Y` | 3 |
| `BTN_COUNT` | 4 |

The default ctor's `*(u32*)(this+0x18) = 4` is therefore `BTN_COUNT` (the count, reused as the
out-of-range/default index), and `SetButtonArrayIfDifferent` (`0xb7b7b0`) taking
`BSTSmallArray<ButtonInfo,4>` matches 4 valid buttons (0..3) -- so the
`buttonIndex >= g_choiceSlots.size()` bound is sound.

Dispatch contract: `OnButtonRelease` does
`MOV RCX,[RAX+RDI+0x10]` → `MOV RAX,[RCX]` → `MOV EDX,ESI` → `CALL [RAX+0x18]`. So the listener stored
at `ButtonInfo+0x10` is called through vtable slot **0x18** (slot 3) with `(rcx=listener, edx=index)`.
`VanillaUISuppressor.cpp`'s `kOnButtonReleaseVtblOffset = 0x18` therefore mirrors the engine, and the
listener is the `ActivateChoiceListener` (stored through its `IMultiActivateUser` base) whose slot 3 is
`OnButtonRelease` @ `0x832b40`.

Caveat on "valid": `ButtonInfo::IsPopulated` returns `listener != 0` (0x10), not the 0x1c flag, and the
ctors set 0x1c = 1 unconditionally -- so 0x1c is real and engine-read, but population is the listener test.
`Detour_PopulateData` was changed accordingly: it now gates on the 0x10 listener (`listener && label`,
the `valid &&` term removed) to match `IsPopulated()`.

Used in: `PrismaUI_F4` `VanillaUISuppressor.cpp` `Detour_PopulateData` / `TriggerActivateChoice`.
`TriggerActivateChoice` also gained `.rdata`/`.text` range guards around the slot-3 dispatch
(mirroring `SuppressHUDWidget`) so a stale listener is refused rather than called through a freed
vtable.

## 2026-08-08 — [CONFIRMED] Workshop GRAB / put-down is one function, and its multi-select bool (1.10.163 OG)

Chased for PrismaCrafting (grab was unimplemented in a full WorkshopMenu replacement).

`Workshop::UpdateActiveEdit` (RVA `0x20b9e0`, Address Library id 69261) is a **single toggle for both
directions**: with nothing held it makes the highlighted/selected edit item the active edit item (grab);
with something held it puts it back down. Its signature is **not** the `(void)` the symbol index
records — the decoded community label at that address is
`?UpdateActiveEdit@Workshop@@<mangled>AXAEBUContextData@1@_N@Z`, i.e.
`void Workshop::UpdateActiveEdit(ContextData const&, bool)`. `check_name_confidence` reports
`unconfirmed_disagreement` for both this and `SetSelectedEditItem` (`0x2077d0`), but in both cases the
plain and decoded labels name the *same* function — the conflict is plain-vs-mangled label form, not an
identity dispute.

The trailing `bool` is **multi-select**, settled from `WorkshopMenu::HandleEvent` (`0xbefe80`) call
sites rather than guessed:

| Call site | Preceded by | Meaning |
|---|---|---|
| `0xbf126b` | `XOR EDX,EDX`, after `RequestRepairItem` (`0x2139a0`) declines | single-item GRAB |
| `0xbf12d3` | `XOR EDX,EDX`, after `CanPlaceItem` (`0x20cc70`) | single-item PUT DOWN |
| `0xbf0fc1` | `XOR EDX,EDX`, after `CanPlaceItem` | put down (second branch) |
| `0xbf15cd` | `MOV DL,0x1`, after `IsValidMultiSelectPlacementItem` (`0x1f3b70`) | MULTI-SELECT |

Vanilla's Accept/Activate eligibility chain around the grab call, in order, all confirmed by name
lookup: `QNewItemMode` `0x21af60`, `HasPlacementItem` `0x20b6c0`, `HasActiveEditItem` `0x20b690`,
`WorkshopCanSelectRef` `0x1f49d0`, `TESObjectREFR::IsSpline` `0x3f4070`, `Workshop::HasRecipe`
`0x1f4780`, `Workshop::IsInventoryItem` `0x1f2e80`, `ItemNeedsRepair` `0x1fb730`,
`nsWorkshopMenu::ItemCanBeRepaired`, `RequestRepairItem` `0x2139a0`. Cancel is
`Workshop::CancelActiveEdit` `0x20b6e0`; `ExpandActiveEdit` `0x219cf0` is multi-select only.

**[CONFIRMED IN GAME 2026-08-08] `WorkshopMenu::HandleEvent` KEEPS RUNNING WHILE THE MENU IS
SUPPRESSED.** It is plain C++ dispatched on abstract **UserEvents** (`QAccept`, `QActivate`,
`QXButton`), *not* on the Scaleform movie, so hiding the SWF does not silence it.

Proven by consequence rather than by decompile. PrismaCrafting bound its own grab to E and, because E is
Accept/Activate, swallowed the key at the raw-input layer so the engine could not also act on it. E then
did **nothing at all** — no grab, no placing the carried item, no pick-up. Three commands died from one
key being withheld, so the engine's handler was serving all three. The plugin's grab implementation was
therefore redundant (vanilla already calls `UpdateActiveEdit` on E) and was reverted.

Consequences for any mod replacing WorkshopMenu:

- **Do not assume a vanilla workshop command is dead because the menu is suppressed** — verify per
  command. PrismaCrafting's scrap genuinely did need reimplementing, which is what made the inference
  tempting, but it does not generalise; whatever breaks scrap's `QXButton` path, it is not an absent
  handler.
- **Withholding a key from the engine in workshop mode is destructive**, because the handler is live and
  one key can carry several commands.

`UpdateActiveEdit` and the eligibility chain above remain correct and confirmed; they are simply not
something a WorkshopMenu replacement needs to call on the Accept/Activate path, because vanilla still
does.

---

## 2026-08-08 — [CONFIRMED] Workshop build-mode HIGHLIGHT pick: a second ContextData, and its full chain (1.10.163 OG)

Chased for PrismaCrafting GitHub #12 (free-fly scrap aimed at the frozen player camera). The workshop
builds **two** `Workshop::ContextData` objects per frame and they drive different things:

| Ctor call site | Consumer | Drives |
|---|---|---|
| `0xcbd478` in `JobListManager::MainRenderAEnd` (`0xcbd450`) | `Workshop::Update` (`0x1ef4e0`), call `0xcbd482` | the placement ghost |
| `0xcbd729` in `JobListManager::AutoAim_PickRef` (`0xcbd710`) | `Workshop::UpdateForegroundReferenceJob` (`0x1f2c20`), call `0xcbd733` | the highlighted/selected edit item |

`AutoAim_PickRef` disassembles to `LEA RCX,[RSP+0x20]` / `CALL 0x1401f8830` (ContextData ctor with
`pPlayer`) / `LEA RCX,[RSP+0x20]` / `CALL 0x1401f2c20`, then tail-jumps to
`PlayerCharacter::UpdatePickRef`. That ContextData is used for nothing else, so a mod may rewrite it
without touching auto-aim or any other subsystem.

Chain below the job, all single-caller, all decompiled from `fwgh_proj_1.10.163_unpacked`:

- `Workshop::UpdateForegroundReferenceJob` (`0x1f2c20`) runs the pick only while
  `Workshop::bEnabled && Workshop::bEditMode && (hPlacementItem == hNull || !Workshop::bActiveEdit)`,
  i.e. not while a ghost is held.
- `Workshop::UpdateForegroundReference<FindWorkshopEditItem>` (`0x22c550`) is a Havok sphere cast from
  ContextData `lookPos` (float words 0–2) along `lookDir` (words 3–5). Cast length is a lazily
  initialised function static, `0x44160000` = **600.0f**. Hits are sorted
  (`hknpAllHitsCollector::sortHits`), resolved via `TESHavokUtilities::FindBodyIdRef`, then filtered:
  skip `TESObjectREFR::pPlayerRefr`, `IsMarker`, refs with an `ExtraDataList::GetPrimitive`,
  `Workshop::IsInteractionBlocked`, and finally `Workshop::FindWorkshopEditItem::IsValid`
  (`0x244ce0`). There is a linked-ref indirection for base-form byte `+0x1a == '&'` → `GetLinkedRef`
  when the target's byte is `')'`.
- Survivors go to `Workshop::FindWorkshopEditItem::operator()` (`0x23c9f0`), which writes the
  selected-edit-item handle through `SetSelectedEditItem` (`0x2077d0`) and notifies
  `BSTEventSource<PickRefUpdateEvent>`.
- **On a miss** the function falls through to `LAB_14022cb2f` and calls `operator()` with `hNull` —
  so the selected-edit-item global is rewritten every frame in both directions. Any external
  `SetSelectedEditItem` write is therefore transient and cannot be used to redirect the highlight;
  the ContextData is the only durable injection point.
- `Workshop::RequestScrapPlacementItem` (`0x1f5330`) reads that same global, gates on
  `Workshop::MultiSelect == false` and `Workshop::WorkshopCanScrapRef`, and passes the ContextData
  through to `RequestScrapRef` without doing any aiming of its own.

Used in `PrismaCrafting/src/WorkshopFreedom.cpp` (`Detour_PickContextDataCtor`, call site `0xcbd729`).

Tooling note for Linux runs of this pipeline: `analyzeHeadless` rejects a project path containing
`&` (`NamingUtilities.checkName`), so the `Tools & Reverse Engineering` tree must be reached through
a symlink without the ampersand; and the projects are owned by the Windows user `Game`, so the run
needs `JAVA_TOOL_OPTIONS=-Duser.name=Game` or it aborts with `NotOwnerException`.

## 2026-08-05 -- Fallout4.exe ships the complete Havok packfile WRITER, not just a reader

Raw decompiles: [Findings/Havok/packfile-writer-decompiled-1.10.163.txt](Findings/Havok/packfile-writer-decompiled-1.10.163.txt).

Chased for BehaviourGraphStudio (`Projects/BehaviourGraphStudio`), which round-trips `.hkx` through
hkxpack's XML and therefore refuses to save any file whose contents that XML cannot carry
losslessly. A native writer removes the XML step and the refusal with it. The Havok 2018 SDK source
cannot supply one: packfiles were removed from Havok before 2018, which only has tagfile read/write
plus a Tagfile2014 *reader*, and FO4 is a packfile (magic `57E0E057 10C0C010`, version 11, layout
rule 8011). The game's own binary turned out to carry the whole thing.

**Symbols present in 1.10.163 (all confirmed by name lookup, `save` and friends decompiled):**

| Symbol | RVA | Note |
|---|---|---|
| `hkSerializeUtil::savePackfile` | `0x1501190` | entry point |
| `hkSerializeUtil::saveTagfile` | `0x15013f0` | present but not what FO4 ships |
| `hkBinaryPackfileWriter::save` | `0x17dfe70` | drives the whole sequence |
| `hkBinaryPackfileWriter::writeAllObjects` | `0x17e0210` | 2284 bytes, the object layout pass |
| `hkBinaryPackfileWriter::doDeferredWrites` | `0x17e0b10` | 1275 bytes, arrays and strings |
| `hkBinaryPackfileWriter::fillSectionTags` | `0x17e0110` | section names |
| `hkBinaryPackfileWriter::writeClassName` | `0x17e0040` | the `__classnames__` section |
| `hkPackfileWriter::setContentsWithRegistry` | `0x17c6800` | takes an `hkVtableClassRegistry` |
| `hkPackfileHeader::hkPackfileHeader` | `0x1503220` | header defaults |
| `hkClass::getMember` / `getMemberByName` | `0x14ff720` / `0x14ff7a0` | member layout access |

**Write sequence, read off `save`:** fill section tags, write the file header, remember the stream
position and reserve the section header array, write all objects, do the deferred writes, then seek
back to the remembered position and write the real section headers.

**Confirmed from `hkPackfileHeader::hkPackfileHeader`:** the header is `0x40` bytes, memset to
`0xFF` first so unset fields read as -1, then magic `0x57E0E057` at 0 and `0x10C0C010` at 4, a zero
byte at `0x28` and a zero dword at `0x38`. **From `fillSectionTags`:** `hkPackfileSectionHeader` is
also `0x40` bytes each, likewise memset to `0xFF` before the name is copied in. Both agree with what
BehaviourGraphStudio's reader already parses, now confirmed from the writing side.

### The fixup tables, from `doDeferredWrites`

Raw decompile: [Findings/Havok/packfile-writer-fixups-decompiled-1.10.163.txt](Findings/Havok/packfile-writer-fixups-decompiled-1.10.163.txt).

`doDeferredWrites` walks the sections and, for each, builds and writes two tables of 32-bit words.
Both are sized `count * 3` and written with `hkOArchive::writeArray32u`, so **each entry is three
`uint32`s**, and both are seeked to using the section header's own offsets rather than written
sequentially.

- **Global fixups**, written at `sectionHeader[0x14] + sectionHeader[0x1c]`. Entry is
  `(sourceOffset, destinationSectionIndex, destinationOffset)`. The source offset is stored relative
  to the section's data start (`... - *(int *)(header + 0x14)` in the decompile). An entry whose
  target does not resolve is left as the `0xFFFFFFFF` the array was prefilled with, which is how an
  unresolved pointer is spelled.
- **Virtual fixups**, written at `sectionHeader[0x14] + sectionHeader[0x20]`. Entry is
  `(sourceOffset, 0, classNameOffset)`. The middle word is written as a literal `0` because virtual
  fixups always point into `__classnames__`, which is section 0, and the third comes from a string
  map keyed by the class name, so identical class names share one string.

That fixes three section header fields by observation: `+0x14` is the section's absolute data start,
`+0x1c` the global fixup table offset, `+0x20` the virtual fixup table offset. The accessors
`hkPackfileSectionHeader::getLocalSize` / `getGlobalSize` / `getExportsSize` / `getImportsSize` /
`getFinishSize` (RVAs `0x174c3d0`, `0x174c390`, `0x174c370`, `0x174c3c0`, `0x174c380`) each compute a
size as the difference between adjacent offsets, which is why the header stores a running set of
offsets rather than sizes.

### Pointers inside one object, from `writeAllObjects`

The third table is built and written in `writeAllObjects` (RVA `0x17e0210`), not in `doDeferredWrites`,
which is why it is a different shape from the other two.

- **Local fixups**, written at `sectionHeader[0x14] + sectionHeader[0x18]`. Entry is a **pair** of
  `uint32`, `(sourceOffset, destinationOffset)`, and both are made section relative immediately
  before the write by subtracting the section's data start from each half:

  ```
  entry[0] -= *(int *)(header + 0x14);
  entry[1] -= *(int *)(header + 0x14);
  ```

  Written with `writeArrayGeneric(archive, table, elementSize = 4, count = pairs * 2)`, so a pair and
  not a triple. Both ends live in the same section, which is what makes it local: a pointer that
  leaves the section becomes a global fixup instead.

**Each table is padded up to a 16 byte boundary with `0xFF`** (`padUp(stream, 0x10, 0xff)`) before
the next one starts, and each table's offset is recorded only after the padding, as
`streamPosition - dataStart`. Anything writing these byte for byte has to pad the same way.

That completes the section header offset map, all four relative to the section's own data start:

| Field | Meaning |
|---|---|
| `+0x14` | absolute file offset of the section's data |
| `+0x18` | local fixup table |
| `+0x1c` | global fixup table |
| `+0x20` | virtual fixup table |

**This is what BehaviourGraphStudio's reader already assumes**, so the confirmation is mutual: the
reader was reverse engineered from the files and the writer says the same thing.

#### The pair shape confirmed a second way, from the struct rather than the writer

The three tables are also reachable from the other end, and it agrees. Fixups are accumulated in an
`hkRelocationInfo` before any of them become bytes: `hkPackfileWriter` hands one down the chain
`hkPlatformObjectWriter::writeObject` (RVA `0x19351f0`) -> `hkObjectCopier::copyObject`
(RVA `0x19c8e00`), and only the packfile writer flattens it. Its constructor (RVA `0x17c7830`) is
**four `hkArray`s at `+0x00`, `+0x10`, `+0x20`, `+0x30`**, matching the four element types
instantiated in the binary: `Local`, `Global`, `Import`, `Finish`.

`hkRelocationInfo::Local::Local` (RVA `0x17e1550`) is the entire struct:

```
Local * hkRelocationInfo::Local::Local(Local *this, int param_1, int param_2)
{
  *(int *)this = param_1;
  *(int *)(this + 4) = param_2;
  return this;
}
```

Two `int`s, 8 bytes, nothing else. So the pair shape is confirmed independently of reading the write
call, and the reason a global needs a third word is visible in the type list rather than inferred: it
names another section, a local cannot.

#### Which int is which, read off the append site

Raw decompiles: [Findings/Havok/addLocal-decompiled-1.10.163.txt](Findings/Havok/addLocal-decompiled-1.10.163.txt),
[Findings/Havok/saveExtras-decompiled-1.10.163.txt](Findings/Havok/saveExtras-decompiled-1.10.163.txt).

The ordering was previously left as forced-by-context. It is now read off the two lines that
actually do it, so it can stop being an assumption.

**Where the entry is stored.** `hkRelocationInfo::addLocal` (RVA `0x17e20f0`) writes both ints in one
store:

```
void hkRelocationInfo::addLocal(hkRelocationInfo *this, int param_1, int param_2)
{
  if (*(uint *)(this + 8) == (*(uint *)(this + 0xc) & 0x3fffffff)) {
    hkArrayUtil::_reserveMore(&hkContainerHeapAllocator::s_alloc, this, 8);
  }
  *(ulonglong *)(*(longlong *)this + (longlong)*(int *)(this + 8) * 8) = CONCAT44(param_2, param_1);
  *(int *)(this + 8) = *(int *)(this + 8) + 1;
}
```

The element stride is 8 and the array is the one at `this + 0x00`, the `Local` array. `CONCAT44(hi,
lo)` puts `param_1` in the low four bytes and `param_2` in the high four, so on a little-endian
target **`param_1` lands at `+0` and `param_2` at `+4`**, agreeing with `Local::Local` writing its
first argument to `+0`.

**What each argument is.** `hkObjectCopier::saveExtras` (RVA `0x19c9fa0`) is the only caller that
appends locals, and every one of its `addLocal` calls has the same shape. Taking the array-of-struct
case:

```
phVar15 = hkOArchive::getStreamWriter(param_3);
iVar7 = (**(code **)(*(longlong *)phVar15 + 0x48))(phVar15);          // tell()
hkRelocationInfo::addLocal(param_7, (uint)*(ushort *)(this_00 + 0x1e) + param_5, iVar7 + param_6);
```

- **First argument** is `*(ushort *)(this_00 + 0x1e) + param_5`. `this_00` is the `hkClassMember`
  being written and `+0x1e` is its `m_offset`, the same field confirmed byte for byte above;
  `param_5` is the offset the object itself starts at. So the first int is **where the pointer field
  sits**.
- **Second argument** is `iVar7 + param_6`, the stream position *just taken* plus the start of the
  extras region, which is the position the pointed-at data is about to be written to. So the second
  int is **the target**.

`(from, to)` therefore, in that order, source first. Confirmed at the append, not inferred from the
flatten. Every other `addLocal` in the function follows the identical `member.m_offset + objectStart`
then `tell() + extrasStart` pattern, so it is one rule and not a per-case accident.

**Still to do before a native writer exists:** every class's member layout needs dumping. The route
is `hkClass`'s own constructor, reached from the `_dynamic_initializer_for__<Class>Class__` set, and
its argument list gives the whole record away:

```
hkClass::hkClass(&hkbVariableBoundsClass, "hkbVariableBounds", parent=0, objectSize=8,
                 interfaces=0, numInterfaces=0, enums=0, numEnums=0,
                 members=&hkbVariableBoundsClass_Members, numMembers=2,
                 defaults=0, attributes=0, flags=0, describedVersion=0)

hkClass::hkClass(&hkbClipGeneratorClass, "hkbClipGenerator", parent=&hkbGeneratorClass,
                 objectSize=0x160, interfaces=0, numInterfaces=0,
                 enums=&hkbClipGeneratorEnums, numEnums=2,
                 members=&hkbClipGenerator::Members, numMembers=0x1c,
                 defaults=&hkbClipGenerator_Default, attributes=0, flags=0, describedVersion=4)
```

`hkbVariableBounds` at 8 bytes with 2 members is the already-known answer, so the reading is sound.

### `hkClassMember` layout, confirmed byte for byte

**The `hkClass` objects do not exist in the file.** `hkbVariableBoundsClass` sits at `0x145b0ae70`
and every byte of it is zero on disk; it is filled in at startup by the dynamic initializer above.
That is why a scan of the data blocks for records matching a guessed `hkClass` layout found three
false positives and no real classes, and it means class metadata can only be read from the
initializers' arguments, never from the static image.

The **member tables are static** and do read straight off the file. `hkbVariableBoundsClass_Members`
at `0x142dff360`, dumped as 8-byte words:

```
+0x00  ptr -> "min"          +0x28  ptr -> "max"
+0x08  0x145b0aec0          +0x30  0x145b0aec0
+0x10  0                    +0x38  0
+0x18  0x0000000000000019   +0x40  0x0004000000000019
+0x20  0                    +0x48  0
```

Stride is `0x28`, and reading the packed word at `+0x18` of each entry as bytes gives:

| Offset | Field | `min` | `max` |
|---|---|---|---|
| `+0x00` | `m_name` (`char*`) | "min" | "max" |
| `+0x08` | `m_class` (`hkClass*`) | same | same |
| `+0x10` | `m_enum` (`hkClassEnum*`) | 0 | 0 |
| `+0x18` | `m_type` (`u8`) | 25 | 25 |
| `+0x19` | `m_subtype` (`u8`) | 0 | 0 |
| `+0x1a` | `m_cArraySize` (`i16`) | 0 | 0 |
| `+0x1c` | `m_flags` (`u16`) | 0 | 0 |
| `+0x1e` | `m_offset` (`u16`) | **0** | **4** |
| `+0x20` | `m_attributes` | 0 | 0 |

Offsets 0 and 4 for `min` and `max` are exactly the known answer, arrived at from the bytes rather
than assumed, so the layout above is confirmed rather than proposed. Type 25 is `struct`, consistent
with both members being an `hkbVariableValue`, which is what makes the class 8 bytes.

### The full sweep

**Full dump: [Findings/Havok/hkclass-field-layouts-1.10.163.txt](Findings/Havok/hkclass-field-layouts-1.10.163.txt)**
(5003 lines). Produced by [Scripts/DumpHkClassInitializers.java](Scripts/DumpHkClassInitializers.java),
which walks the `_dynamic_initializer_for__*Class__` functions, reads the `hkClass::hkClass`
arguments, and then reads each member table with the layout above. It flushes per class so a run
that dies partway keeps what it reached.

The per-class layouts are in that file rather than inline here: 950 classes at roughly five lines
each would bury this log. Each entry is `+offset  name  type`, which is the whole of what the write
path needs.

**950 classes read. 2 initializers could not be parsed and 1 member table could not be resolved**
(`hkcdStaticMeshTreeBasePrimitiveDataRunBase<hknpCompressedMeshShapeTreeDataRunData>`, a physics
mesh tree template, not on the behaviour or animation path). By family: 268 `hkb*`, 85 `hkc*`,
83 `hkp*`, 36 `hka*`, 26 `hkx*`, the rest core `hk*`.

**Those three are left as known gaps, deliberately.** Neither the physics mesh tree template nor the
two unparseable initializers sits on the behaviour or animation path, which is what the write path
needs, and both were checked for that rather than assumed. They are recorded here so coverage reads
as 950 of 953 with three named exceptions rather than as complete. Anyone extending this to the
physics side should start by closing them.

The ground truth check still passes at the end of the sweep, which is the point of leaving it in the
file header:

```
class hkbVariableBounds : -  size=8  members=2
    +0     min      struct
    +4     max      struct
```

And the class the editor cares about most comes out matching the XML that hkxpack produces for the
same object, field for field and in order:

```
class hkbClipGenerator : hkbGeneratorClass  size=352  members=28
    +136   animationBundleName          stringptr
    +144   animationName                stringptr
    +152   triggers                     pointer of struct
    +160   userPartitionMask            uint32
    +164   cropStartAmountLocalTime     real
    +168   cropEndAmountLocalTime       real
    +172   startTime                    real
    +176   playbackSpeed                real
    +180   enforcedDuration             real
    +184   userControlledTimeFraction   real
    +188   animationBindingIndex        int16
    +190   mode                         enum of int8
    +191   flags                        int8
    ...
```

**Two lookup traps worth recording**, because each silently lost layouts rather than erroring:
member tables are named three ways, flat (`hkbVariableBoundsClass_Members`), namespaced
(`hkbClipGenerator::Members`), and doubly namespaced (`hkbStateMachine::StateInfo::Members`).
Matching on the whole string lost 189 of 950; matching on the last segment against the full path as
the namespace still lost 19, because Ghidra reports only the immediate parent. Matching the last
segment against the second to last is what gets all but one.

#### Cross-checked against a second, independent sweep

A separate sweep was written and run against the same binary without reference to the one above
(`Scripts/SweepHkClasses.java`, which reads the same constructor arguments but out of the
decompiler's printed C). The two agree on 3946 of the roughly 4040 member lines. Every disagreement
was resolved against the sweep above by one test that needs no outside knowledge: **a member cannot
sit at or past its own class's object size.**

| | members outside the object |
|---|---|
| `DumpHkClassInitializers.java` (the file above) | 0 |
| `SweepHkClasses.java` (the second run) | 29 |

The second run's failures were all the member-table lookup trap named above biting in a different
place: `hkbBehaviorGraphData` is 112 bytes, and it produced members at +136 through +200, which is
another class's table entirely. Its output has been deleted rather than kept alongside, so there is
one class table in this folder and not two that disagree. The script stays, with the caveat that its
symbol resolution is the weaker of the two and it needs the size check adding before it is trusted
again.

Worth stating plainly: the cross-check's value was not that it produced a second table, it was that
it produced a *falsifiable* one. Two sweeps agreeing on a bad lookup would have proved nothing; the
size test is what settled it.

---

## 2026-07-23 -- Landscape/exterior-cell system catalogued (Workshop-Notes-style), applied to OpenCommonwealth's exterior cell compiler

Full writeup: [Organized/1.10.163/README-Landscape.md](Organized/1.10.163/README-Landscape.md).
Driven by a request to improve OpenCommonwealth's (`Projects/OpenCommonwealth`) cell compiler's
exterior-cell support, which had none (interior-only: `CellCompiler.ListInteriorCells` filtered
strictly on `Cell.Flag.IsInteriorCell`, no LAND/terrain handling existed at all).

**Confirmed via decompile, not assumed from community VHGT lore:**
- Cell size is exactly 4096 game units (`TESObjectLAND::GetWorldCellX`, RVA `0x3a0730`: `cellX <<
  0xc`). Quad pitch follows as 4096/32 = 128 units.
- `TESObjectLAND::LoadVerticesIntoArrays` (RVA `0x3a09e0`) confirms the VHGT chunk is exactly
  `0x448` bytes (4-byte `Offset` float + 33x33 signed-byte deltas + 3 unused bytes, matching
  Mutagen's already-modeled `LandscapeVertexHeightMap`), confirms the row-boundary modulus is 33
  (`0x21`), and confirms the finalize step is `(int)accumulatedHeight << 3` -- i.e. **the well-known
  "×8" VHGT scale factor is real, verified against this exact binary**, not carried over from
  memory.
- `TESObjectREFR::GetPosition()` (`RE/T/TESObjectREFR.h:379`) returns `data.location`
  unconditionally -- no interior/exterior branch, no cell-grid offset ever applied. Combined with
  the `GetWorldCellX`/`GetCellX` decompiles (which derive the grid coordinate FROM the world
  position via `>> 12`, never the reverse), this confirms a cell's grid X/Y is a spatial-partition
  index only -- exterior placed-reference positions are already world-absolute in the record data,
  same coordinate space regardless of interior/exterior. This is why `CellService.cs` (already
  interior/exterior-agnostic) needed zero changes to work correctly for exterior cells.
- `CHAR_NORM` (`RE/C/CHAR_NORM.h`) is a plain signed 3-byte struct, and the VNML load path
  (`0x4c4d4e56` branch in the same function) does a raw byte copy with no rescale -- confirms
  on-disk vertex normals are signed bytes, decoded as `sbyte/127.0f` per axis, not an unsigned
  0-255 range.

**Applied same day:** `OpenCommonwealth/Services/CellCompiler.cs` gained `ListExteriorCells` (was
interior-only) and `BuildTerrainMesh` (real per-cell heightmap geometry + real per-vertex normals/
colors from VNML/VCLR, using the confirmed formula above), wired through a new `--exterior` flag on
`CellCompilerMain.cs`. `CellService.cs` now also surfaces `Landscape`/`Grid` off the cell record.
Texture-layer blending (BTXT/ATXT/VTXT) is not implemented -- terrain ships with a flat
vertex-colored material, tracked as a known gap in that project's own `ROADMAP.md`.

---

## 2026-07-23 -- Player controller fully catalogued, f4-re-mcp ABI bug fixed

Full writeup: [Organized/1.10.163/README-PlayerController.md](Organized/1.10.163/README-PlayerController.md)
(also on the OpenCommonwealth GitLab wiki, `Player-Controller-Notes` page, dev-facing version with
no local paths). Driven by a request for a full RE scope of the player controller for the Godot
port.

**f4-re-mcp was broken.** Every lookup/vtable tool call failed with a `better-sqlite3`
`NODE_MODULE_VERSION 127 vs 146` mismatch, the installed `.mcpb` extension's native module was
built for a different Node ABI than the host loads. Root cause: ABI 146 is Electron
`42.0.0-alpha.1` per the `electron/node-abi` registry, and `better-sqlite3@11.10.0`'s native source
doesn't compile against that V8 (`v8::External::Value()` signature changed). Fixed by upgrading to
`better-sqlite3@13.0.1`, which ships N-API prebuilds that are ABI-stable across Node/Electron
versions (confirmed by loading the stock prebuild directly, no rebuild needed). Updated in
`ReverseEngineering/re-mcp/package.json`, repacked, deployed into the installed Claude Extensions
copy. Needs a reconnect/restart of the live MCP connection to pick it up, a running process keeps
the old files loaded in memory.

**Documentation gaps closed by cataloguing** (writing new CommonLibF4 headers is still separate
follow-up work, not done here):
- 7 of `PlayerControls`' registered handler classes have no CommonLibF4 header:
  `MovementHandler`, `ActivateHandler`, `RunHandler`, `SprintHandler`, `ToggleRunHandler`,
  `TogglePOVHandler`, `GrabRotationHandler`. Vtable slots and full struct layout confirmed for all 7
  (`FindDumpVtable.java` + every constructor decompiled). `MovementHandler`/`ToggleRunHandler` are
  direct `PlayerInputHandler` subclasses (11 slots), the other 5 are `HeldStateHandler` subclasses
  (13 slots).
- `PlayerInputHandler.h` itself is incomplete: every subclass's vtable has a `ShouldHandleEvent`
  slot right after the destructor-family slot, CommonLibF4's header only documents `PerFrameUpdate`
  as an added virtual.
- All 13 `CameraStates` enum values resolved to real classes except `kAnimated`, which is confirmed
  to just not exist: decompiling `PlayerCamera::SetCameraRoot` (the actual state-array factory,
  `PlayerCamera::PlayerCamera` doesn't build them) shows it writes all 13 array slots and simply
  skips slot 7, going straight from `TweenMenuCameraState` (slot 6) to `ThirdPersonState` (slot 8).
  Found the other 4 real names by grepping for the `Class::Class(Class*, TESCamera*, uint)`
  constructor signature across the whole binary, which surfaced the whole cluster at once,
  including two states with no prior public reference anywhere: `IronSightsState`,
  `PlayerCameraTransitionState`, `TweenMenuCameraState`, `HorseCameraState` (the `kMount` state,
  named after horses despite FO4 having no ridable horses, presumably inherited naming from an
  earlier engine generation). Struct size and inheritance base confirmed for all 13 slots from the
  same `SetCameraRoot` decompile (some extend `TESCameraState` directly, others extend
  `ThirdPersonState`, mixed, not all flat).
- `MovementControllerNPC` (83 functions) inventoried, and its actual frame-to-frame movement path
  decompiled: `ProcessMessages` is the per-tick pump, swaps the message/warp queues under a spin
  lock and forwards each message through `ProcessMessage` into a global
  `ActorMovementMessageMap<16>` singleton keyed by message type. The real per-message movement
  behavior lives in that message map, not chased further this pass.
- 12 core algorithms decompiled directly (WASD-to-move-vector translation, the E-key activate
  decision tree, which also answers 04-ROADMAP.md's open item #1, POV toggle, sprint/run/grab
  rotation handling, `PlayerCharacter`'s movement filter chain, the locomotion direction-blend
  bitmask builder, the main camera update loop, and the `MovementControllerNPC` wiring).

Only remaining follow-up: nobody's written the actual CommonLibF4 headers for the 7 handler classes
or the 12 camera states yet, this pass pinned down everything a header would need (vtable, size,
base class) but didn't produce the header files themselves.

New CSV (regeneratable, gitignored like the rest of `Organized/1.10.163/*`):
`Organized/1.10.163/player_controller_functions.csv`, 663 rows across every class named above.

---

## 2026-07-16 — The player death/reload pipeline: how vanilla ends a death, and two bugs it caught in Permadeath

Driven by two user reports against the Permadeath plugin ("no death screen", "the loading screen
after death freezes"). Both turned out to be engine behaviour the mod never accounted for. Every
address below is from `fwgh_proj_1.10.163_unpacked`.

### The death-reload flow — `PlayerCharacter::UpdateWhenAIControlledOrDead` (RVA `0xebd5d0`)

Runs once per frame and owns the whole "you died" outcome:

```c
if (fPlayerDeathReloadTime <= 0.0) {
    if (playerLifeState == 2 && !bShowDeathMessage) {
        Actor::DetachCharController(player);
        if (BGSSaveLoadManager::HasMostRecentSaveGame())
            MessageMenuManager::Create("...", "Reloading the most recent save", <DefaultMessageBoxCallback wrapping PlayerCharacter::DeathMenu>,
                                       kInGameMessage, "Reload", "Main Menu", ...);
        else
            Main::resetGame = true;                      // Main+0x25
        bShowDeathMessage = 1;
    }
} else if (playerLifeState == kDying || playerLifeState == kDead) {
    if (fplayerDeathReloadTime < 0.0)
        fplayerDeathReloadTime = fPlayerDeathReloadTime; // seeded ONCE, only while negative
    fplayerDeathReloadTime -= deltaT;
    if (fplayerDeathReloadTime < 0.0) {
        fplayerDeathReloadTime = -1.0;
        SubtitleManager::KillSubtitles();
        if (BGSSaveLoadManager::HasMostRecentSaveGame())
            BGSSaveLoadManager::QueueSaveLoadTask(kLoadMostRecentSave);
        else
            Main::resetGame = true;
    }
} else {
    fplayerDeathReloadTime = -1.0;                       // reset whenever the player is alive
}
```

| Symbol | RVA | Notes |
|---|---|---|
| `fPlayerDeathReloadTime` (Setting object) | `0x371b820` | `.data`. `SettingT<INISettingCollection>`: vtable +0x00, `_value.f` **+0x08** (`0x371b828`), `_key` +0x10 -> `"fPlayerDeathReloadTime"` (`0x2cc30b0`). **Default 5.0.** Read from exactly one place, the function above. |
| `fplayerDeathReloadTime` (countdown) | `0x37959fc` | `.data`, writable. Transient. Note the lowercase `p` is the only thing distinguishing it from the Setting. |
| `bShowDeathMessage` | `0x5ac7bb6` | `.data`. Only gates the `<= 0` branch. |
| `PlayerCharacter::DeathMenu` | `0xeb03a0` | The vanilla death box's callback. `arg 1` -> `BGSSaveLoadManager::LoadMostRecentSaveGame`; `arg 2` -> `Main::resetGame = true`. Clears `bShowDeathMessage` on entry. |
| `Main::GameOver` | `0xd4d150` | Just `return this[0x24]` (`Main::quitGame`). Not the death path — do not confuse it with one. |

**Reusable facts.** (1) Any mod that wants to own the death outcome must suppress this, or the engine
loads a save out from under it ~5 seconds later. (2) Writing the *countdown* is the clean lever: the
seed only happens while it is negative, so a large positive value written before the first dead frame
wins, and the engine resets it to `-1.0` itself once the player lives again, so there is nothing to
restore and no persistent INI setting left stomped. (3) "Return to Main Menu" via `Main::resetGame`
is not a mod hack — it is exactly what vanilla's own death box does.

### `TESDeathEvent` fires TWICE per death

Both sends are inside `Actor::KillImpl` (RVA `0xdb27b0`), same frame:

| Sender | RVA | `dead` | life state |
|---|---|---|---|
| `TESObjectREFR::Events::SendDying` | `0x4357e0` | `false` | `kDying` (1) |
| `TESObjectREFR::Events::SendDeath` (via `Actor::DoDeathStuff`, RVA `0xda8870`) | `0x435740` | `true` | `kDead` (2) |

`Actor::KillImpl` early-outs if the life state is already `kDying`, so it cannot re-enter for one
death; `PlayerCharacter::KillImpl` (RVA `0xebaee0`) notifies `BGSActorDeathEvent` (a *different*
event) before delegating to it.

### `Actor::IsDead` is NOT "has finished dying" — RVA `0xd89d40`

```c
lifeState = (this+0x130 >> 0x11) & 0xf;      // 4-bit ACTOR_LIFE_STATE
if (notEssential) return lifeState in {1,2,5};
else              return lifeState <= 7 && (0xa6 >> lifeState & 1);   // {1,2,5,7}
```

`0xa6` = `0b10100110`, bits 1 and 2 set, so **`IsDead(false)` is already true at `kDying`**. Any
plugin sinking `TESDeathEvent` and gating on `IsDead(false)` therefore runs its handler twice per
death. This is what it caught in Permadeath: two GAME OVER dialogs, two callbacks each able to set
`Main::resetGame` (the second during `PerformGameReset`), and a grave archived away by the second
run before it ever spawned.

**Misname caught:** CommonLibF4 also has `Actor::IsDead` at RVA `0xd8dd00`, which decompiles to a
`HitData` + `Actor::HitMe` wrapper — it is not IsDead at all. Another instance of the RTTI-matching
misnames catalogued in `07-DECOMPILE-CANDIDATES.md`. The real one is `0xd89d40`.

### `MessageMenuManager::Create` drop rules — RVA `0x12ad190` -> `Create_VariableButtons` (RVA `0x12ad4e0`)

The message is dropped when `disabledWarnings[context]` is set, when `context == 0x22`, when
`UIMessageQueue` is null, or when `messages.size() > 19` **unless** `context == kInGameMessage`
(`0x1A`) — which is why vanilla's own death box uses `kInGameMessage`. There is also an unconditional
bail at the top if `TES::runningCellTests` / `runningCellTests2` (`TES+0xc5`/`+0xc6`) are set; those
are dev flags and normally false, so they are not a real-world failure mode.

### `fPlayerDeathReloadTime` is a GameSetting, not an INI setting — and the OG/NG ID-space split

Despite the `f` prefix, its dynamic initializer (RVA `0x29e6bf0`) disassembles to:

```
CALL 0x1400240b0                  ; GameSettingCollection::InitCollection  (= id 948832 -> 0x240b0)
MOV  RCX, qword ptr [0x1458e1030] ; GameSettingCollection::Singleton       (= id 8308   -> 0x58e1030)
LEA  RDX, [0x14371b820]           ; the fPlayerDeathReloadTime Setting object
CALL qword ptr [RAX + 0x8]        ; vtable slot 1 = SettingCollection::Add
```

So **`RE::GetINISetting("fPlayerDeathReloadTime")` returns null** (it only searches
INIPref/INISettingCollection). The working lookup is
`GameSettingCollection::GetSingleton()->GetSetting("fPlayerDeathReloadTime")`. The key string at
`0x2cc30b0` is `"fPlayerDeathReloadTime\0"` — no `:Category` suffix, so the by-name match is exact.
Writing that Setting's `_value.f` (+0x08) writes the exact float the engine reads as `DAT_14371b828`.
This is the version-independent lever: `GameSettingCollection::Singleton` is
`REL::VariantID{ 8308, 2690301, 4797590 }`, covering all three runtime slots.

**The OG and NG/AE Address Library ID spaces are completely different databases.** Verified by
parsing every bin in `CommonLibF4\AddressBins\F4SE\Plugins\`:

| Bins | Slot | Count | ID range |
|---|---|---|---|
| `1-10-130/138/162/163` | `kOG` (163 only) | ~1.58M | dense `0 .. 1583368` |
| `1-10-980`, `1-10-984` | `kNG` | ~594k | sparse `15 .. 3.6M` |
| `1-11-137 … 1-11-221` | `kAE` | ~651k | sparse `15 .. 10.26M` |

Slot rule from `REX/FModule.cpp`: `kOG` = version **exactly** 1.10.163; `kNG` = >1.10.163 and
<=1.10.984; `kAE` = >1.10.984. **The current retail 1.11.221 is `kAE`.** `REL::VariantID{og, ng}`
fills the third slot with the last value, so two ids cover AE with the NG id.

**A bare `REL::ID(x)` in a multi-runtime plugin is a bug**, and a silent one. It fills every slot
with the same id, and `IDDB::offset` (format <5 path) does a `std::lower_bound` that only
`REX::FAIL`s when the iterator hits `end()` — a missing id with any larger id after it returns
**the next id's offset**. Caught exactly this in Permadeath: `REL::ID(179769)` for
`ImageSpaceModifierInstanceForm::Trigger` (correct on OG) does not exist in any NG/AE bin, so on
NG/AE it silently resolved to an unrelated function. CommonLibF4 already declares it
`VariantID{ 179769, 2199906 }`.

Useful reverse-lookup results while mapping this subsystem (OG id space):
`fplayerDeathReloadTime` countdown -> id **904394**; `fPlayerDeathReloadTime` Setting -> id
**554275**; `UpdateWhenAIControlledOrDead` -> id **851850**; `Actor::KillImpl` -> id **319011**;
`Actor::IsDead` -> id **624191**; `bShowDeathMessage` -> id **1256977**. Note id 554275 *also
exists* in the NG/AE space but points at something unrelated — proof that OG ids must never be
reused across the boundary.

### GameSettingCollection's BTree is ordered by interned POINTER, not string content

Worth knowing before trusting (or distrusting) `GameSettingCollection::GetSetting`. CommonLib's
`SettingCollectionMapCompare` compares `BSFixedString::c_str()` **pointers**, which looks like a bug
until you check the engine. `BSTBTreePolicy<BSFixedString, Setting*, SettingCollectionMap<Setting>::Entry,
BSTObjectArenaHeapAlloc, 128, 0>::InsertEntry` (RVA `0x1d380`) binary-searches with:

```c
pcVar5 = BSFixedString::QString(param_4);                                  // key's interned char*
pcVar6 = BSFixedString::QString((BSFixedString*)(param_1 + uVar9 * 0x10)); // node entry's char*
if (pcVar6 < pcVar5) { ... }                                              // POINTER comparison
```

So the engine really does order these trees by interned-pointer value, and CommonLib's comparator
matches it exactly. `GameSettingCollection::GetSetting(name)` is therefore a correct,
version-independent way to reach any hardcoded game setting's live float — no address needed. The
same applies to the other `SettingCollectionMap<Setting>` trees.

Full write-up: `docs\Code Notes\Permadeath.md`, `docs\Prisma\findings\permadeath.md`.

## 2026-07-14 — Weather round 9: `SkySound` construction site found, rain-occlusion system found real (round-8 retraction was too broad)

Closed the last three open items from round 8's gaps note. Two real corrections came out of this round
— both left in place rather than silently fixed:

**`Sky::FindSkySound` has zero direct callers** (`FindCallers.java` against its RVA: none). The real
construction site is **`Sky::ActivateWeatherSounds`** (RVA `0x654800`), which implements the identical
dedup comparison *inlined* rather than calling the named function — `FindSkySound` is real and
correctly-shaped but dead/orphaned in this build. `ActivateWeatherSounds` also surfaced a brand new
field, **`TESWeather+0xDC8`**: the weather record's own per-weather sound-descriptor list (CK's
"Sounds" list, runtime form) — not documented anywhere before this. Closes the `SkySound::
ucThunderCount` lifecycle completely: incremented here on Thunder-`SkySound` activation, decremented
in `Sky::UpdateSound` on cleanup — it's a live count of active Thunder sounds, not a jitter value. This
sharpens the round-1 `ThunderFreq==0` crash: the real divisor is `ThunderFreq * ucThunderCount`, so an
authored zero remains the dominant trigger, with a narrower transition-race path also theoretically
possible.

Reading further into **`Sky::UpdateSound`**'s scheduling body (previously only its list-cleanup
preamble was covered) resolved two more `weatherData[]` bytes: **`[8]`/`[9]`** (`+0xA74`/`+0xA75`) are
the Thunder onset-threshold percentage pair (current/last weather); **`[6]`/`[7]`** (`+0xA72`/`+0xA73`,
already read by `Precipitation::Update`) are confirmed as an ambient-sound fade-threshold pair shared
by design with precipitation's own intensity fade — **correcting round 6's tentative "precipitation
density/intensity" guess** to the better-supported "shared ambient/precipitation fade threshold."
Also found: Thunder hits can trigger a weather-attached spell via `TESWeather::CheckApplySpell` —
previously undocumented lightning-strike-damage-style mechanic.

**Round 8's retraction of `Precipitation::RenderOcclusionMap`/`ComputeProjection`/
`UpdateRainOcclusionData` was wrong in a specific way**: they don't exist as *virtual* methods (true,
confirmed by the vtable dump), but they DO exist as real non-virtual member functions — found instantly
via a plain symbol search once someone actually looked instead of trusting the vtable dump as
proof of non-existence. Decompiled all three: `RenderOcclusionMap` confirms a real D3D occlusion
render-target pipeline (slot `0x56`/depth-stencil `8`) and the new field `Sky+0xA0` (`Precipitation*`
singleton); `UpdateRainOcclusionData` closes the provenance of the `kRainDirection` global (wind +
particle-system fall-direction, previously only known via its read-only accessor); `ToggleRainOcclusion`
is the trivial flag-flip pairing with both globals `RenderOcclusionMap` reads.

**Lesson recorded in `README-Weather.md`**: a vtable dump proves a class's virtual surface, not its
entire surface. Retracting a name because it isn't virtual is a different claim than retracting it
because it doesn't exist — round 8 conflated the two, corrected here.

## 2026-07-14 — Weather round 8: `Atmosphere`/`Stars` vtables closed, `SkySound` confirmed non-polymorphic

Finished the `FindDumpVtable` sweep round 7 started. Dumped `Atmosphere` and `Stars` — both follow the
exact same 3-slot shape as `Clouds`/`Precipitation` (destructor, inherited `SkyObject::GetRoot`, one
`Update` override) with zero other virtuals; decompiled both destructors to confirm (trivial, same
shape as `Precipitation::Func1`). **This makes the pattern conclusive**: every `SkyObject` subclass
touched this sweep shares one 3-slot vtable shape — `SkyObject` itself is the real polymorphic
surface, subclasses only ever override `Update`. Retracted `Atmosphere::Init` from the prior gaps
note — it doesn't exist, another unverified guess.

`SkySound` has **no vtable symbol at all** — confirmed via `FindDumpVtable` (no match) plus its own
constructor (writes no vtable pointer) and storage (`BSSimpleList<SkySound*>`, a plain linked list,
not a polymorphic collection). **`SkySound` is not a `SkyObject` subclass** — a lightweight value
struct, despite CommonLibF4 reserving an RTTI/VTABLE `REL::ID` slot for it (likely a type-descriptor
reservation for an unrelated purpose, not evidence of an actual vtable). Decompiled
`Sky::FindSkySound(BGSSoundDescriptorForm*, TESWeather*)` (RVA `0x654A50`) to confirm the storage
mechanism: walks a linked list at **`Sky+0x370`** (newly confirmed field — the `BSSimpleList
<SkySound*>` head) doing an identity dedup check on the same two fields the constructor sets,
returning true on a match — a "don't create a duplicate SkySound for this weather+sound combo" guard.

**Status**: the full virtual-method surface of every weather-adjacent polymorphic class touched this
sweep (`Clouds`, `Precipitation`, `Atmosphere`, `Stars`, `TESRegionDataWeather`) is now closed and
verified via real vtable dumps, not guesses. `SkySound` is confirmed to have no virtual surface to
close. Remaining work is exclusively non-virtual functions and unexplored call sites (e.g. where
`Sky::FindSkySound`/the `SkySound` constructor actually gets called from — not yet traced).

## 2026-07-14 — Weather round 7: gap-closing via real vtable dumps, 3 guessed function names retracted

Follow-up to round 6's "genuine gaps" list, which named `Precipitation::RenderOcclusionMap`/
`ComputeProjection`/`UpdateRainOcclusionData` and `Clouds::RemoveTextures` as untouched targets —
those were unverified guesses, not confirmed names. `FW_Query.java` exact-name search returned zero
matches for all of them. Ran `FindDumpVtable.java` against `Precipitation`, `Clouds`, and
`TESRegionDataWeather` to get the REAL virtual method lists instead of continuing to guess.

**Result: the guessed names don't exist.** `Precipitation` has exactly one virtual method beyond
`Update` (the destructor, `Func1` @ `0x64B040`) — no occlusion/projection virtual surface at all.
`Clouds` has exactly three virtuals total (destructor, inherited `SkyObject::GetRoot`, `Update`) — no
`RemoveTextures`. Both guesses are retracted in `README-Weather.md` rather than left as dangling open
items that don't correspond to real functions.

**`TESRegionDataWeather` had 4 real, previously-unknown virtual members** the vtable dump surfaced:
`GetID` (trivial, returns `3` — directly confirms `REGION_DATA_ID::kWeather == 3`, previously only
inferred), `Copy` (confirms the class's only payload beyond `TESRegionData` base is a `TESWeatherList`
at `+0x10`), `Blend` (a genuinely new method, distinct from `BlendInto` — composes `Copy` +
`BlendInto(other, mode=2)`), `Validate` (empty-weather-list check). All 4 decompiled and closed out.

**Also corrected a round-6 writeup mistake while re-verifying**: `Clouds::Update`'s two 32-byte
`TESWeather` arrays (`+0x228`/`+0x248`) were originally described as a day/night pair — re-reading the
exact `pCloudTexCoordOffset.x`/`.y` write pointer arithmetic shows there's no time-of-day input in
this function at all; they're two independent per-layer scroll-speed **axes** (X/Y), blended only
current-vs-last-weather via `currentWeatherPct`. Fixed in place in `README-Weather.md`, not left wrong.

**Genuinely still open**: `Atmosphere`/`Stars`/`SkySound` haven't had the same `FindDumpVtable`
treatment yet — their virtual surfaces are still only known via ad-hoc address decompiles, not a
verified full vtable enumeration. `Precipitation::Update`'s `weatherData[6]`/`[7]` bytes (rain
density/intensity) remain unnamed at the CK-field level.

## 2026-07-14 — Weather round 6 (final sweep): smooth-transition public API, water ripples wired to rain, Clouds/Atmosphere/Stars/Precipitation entrypoints closed out

Exhaustive close-out pass per explicit instruction ("find everything possible then report back").
Read the entire remaining decompiled-but-unwritten backlog (7 items) plus 3 further already-recorded
functions found while closing it out — no new Ghidra decompiles needed, `DECOMPILE-RECORDS.md` already
had everything (103 functions, category `Weather`). Full writeups in `README-Weather.md`.

**`Sky::SetWeatherExternal`** — the real public smooth-weather-set API (counterpart to `ForceWeather`'s
hard snap). Sets `overrideWeather`/`defaultWeather` without forcing a snap, and when interrupting an
in-progress transition, stashes the current `currentWeatherPct` into `Sky+0x390` and sets `Sky+0x394`
bit `0x8` — **this is the mechanism behind a blend-acceleration branch round 1 found in
`Sky::UpdateWeather` but couldn't fully explain** (`(Sky+0x394 & 8)` gating a pct-recompute using
`Sky+0x390` as an anchor). Confirms the anchor lets a new weather change resume smoothly from wherever
an interrupted transition was, rather than snapping back to 0%. Practical implication: Rad Mod's
hand-rolled `BeginSmoothWeather()` may be reimplementable as a direct call to this function.

**`Precipitation::Update`** — new, previously undocumented cross-system connection: rain intensity
(blended via `currentWeatherPct` from two new, unnamed `weatherData[]` bytes at index 6/7) is wired
directly to **`TESWaterSystem::ToggleWaterRipples(bool, float)`** — confirms vanilla rain-driven water
ripples are a real, callable, weather-percentage-gated engine feature, not a hardcoded rain check.

**`Clouds::Update`** — closes CommonLibF4's zero coverage of `Clouds`. The actual per-frame cloud-scroll
driver: reads per-layer day/night scroll-speed bytes from two previously-unlabeled 32-byte
`TESWeather` arrays (`+0x228`, `+0x248`), blends day/night then current/last-weather, and accumulates
into `BSSkyShader::pCloudTexCoordOffset[layer]` each frame — literally why clouds drift.

**`Atmosphere::Update`** — closes zero coverage of `Atmosphere`; the shader-constant pushdown that
feeds already-blended `Sky` fog/wind/color state to the GPU each frame. Independently
cross-confirmed the `fogData[0]`=Near/`[1]`=Far index mapping by using it the same way `Sky::UpdateFog`
does.

**`Stars::Update`** — closes zero-coverage gap; star opacity fades via sunset/sunrise window
*midpoints* (narrower than the day/night boundary logic used elsewhere) and is forced to 0 if the
current weather's `Sky+0xF0/F4/F8` color is pure black (overcast can suppress stars outright). Also
manages a real ref-counted streamed star-mesh resource.

**`TESRegionDataWeather::GetRandomWeather`/`UsesWeather`**, **`TESClimate::GetWeatherOfType`** — confirm
the weighted-random-with-live-global-override algorithm is identical at every level of the
weather-selection hierarchy (Climate, WeatherList, RegionDataWeather) — one algorithm, not three.
`GetWeatherOfType` is the classification-bit ("get the rainy/snowy/etc weather of this climate")
lookup, same dynamic-chance mechanic.

**`Sky::UpdateHDRValues`**, **`Sky::GetFogValuesFromTemplate`** — closed the remaining round-1 backlog.
`GetFogValuesFromTemplate` nails down the exact fog-source priority order (explicit template → cell
override → Sky's cached room template → hardcoded Sky-instance fallback) and a hardcoded `163840.0`
absolute fog-far ceiling clamp, independent of any authored data.

**Status**: every function in the round-1 through round-5 "not yet examined" backlog is now closed.
`Clouds`/`Precipitation`/`Atmosphere`/`Stars`/`SkySound` all have their main per-frame entrypoint
covered (previously zero coverage on all five). Remaining real gaps: `Precipitation`'s
rain-occlusion-shadow-map side (`RenderOcclusionMap`/`ComputeProjection`/`UpdateRainOcclusionData`,
distinct from the particle-spawn side already covered), the exact X-vs-Y byte split inside `Clouds
::Update`'s per-layer scroll read, and `TESRegionDataWeather::Copy`/`Validate`/its remaining
constructors. See `README-Weather.md`'s "Genuine gaps remaining after this pass" for the full list.

## 2026-07-11 — Weather round 5: weather camera-effect spawn/despawn thresholds, aurora crossfade, a third `kFlags` bit

Read the remaining round-1 decompiled-but-unwritten backlog (`Sky::UpdateEffects`, `Sky::UpdateAurora`,
`Sky::SkipWeatherTransition`) — no new decompiles this round, purely synthesis of already-recorded
evidence, closing out most of the round-1 backlog.

**`Sky::UpdateEffects`** sharpens the CHANGELOG.md's "VE instantiated once" claim into precise
mechanics: two effect slots (current + outgoing, the current gets demoted to outgoing on a weather
change rather than destroyed), weather-attached magic spells (`weatherSpellData[2]`) get explicitly
`QuickRelease`d/`Dispel`ed during transitions, and — the real new finding — **camera effects don't
spawn/despawn at fixed transition-percent thresholds**, they use two independent CK-authored
`weatherData[]` bytes (`[0xF]` spawn threshold, `[0x10]` despawn threshold) so a modder controls
exactly when during a transition the visual effect appears.

**`Sky::UpdateAurora`** confirmed an identity-checked crossfade (switching between two weathers that
share the same aurora object skips the fade entirely) and surfaced a **third confirmed
`weatherData[kFlags]` bit**: `0x10` gates a time-windowed aurora opacity ramp, alongside the
already-known `0x4` (`kRainy`) and `0x40` (rain-occlusion) bits.

**`Sky::SkipWeatherTransition`** confirmed as a trivial, real, callable primitive (sets `Sky+0x394`
bit `0x1000000`) — a lighter-weight way to snap an in-progress transition than `ForceWeather`'s full
state reset, though where the flag gets consumed wasn't traced this pass.

## 2026-07-11 — Weather round 4: the full wind pipeline, and a self-caught wrong assumption corrected before it shipped

Read the round-1 decompiled-but-unwritten backlog first (`Sky::UpdateWind`, `Sky::
UpdateColorsFromTemplate`, `Sky::UpdateFogFromTemplate`), then 5 more through `RecordFunctions.java`
(103 total).

**`Sky::UpdateWind` closes round 2's unnamed `Sky+0x31C`/`Sky+0x320` wind fields**: confirmed
`weatherData[0]` (`kWindSpeed`) → `Sky+0x31C` → `BSShaderManager::fWindMagnitude`; `weatherData[0x13]`
(`kWindTurbulence`) → `Sky+0x324` → `fWindFrequency`/`fWindMinSpeed`/`fWindMaxSpeed`; `weatherData
[0x11]`/`[0x12]` → a jittered wind-direction algorithm (4 random offset samples, keeps whichever lands
closest to the base angle) → `BSShaderManager::fWindAngle`. All four use the exact same
weather-transition blend as fog (round 1) — wind is a first-class transitioning property, not a snap.

**Self-correction, caught before it misled anyone**: `Sky::UpdateColorsFromTemplate`/
`UpdateFogFromTemplate` were queued under the assumption they were the "purple sky"
weather-template-inheritance bug class from Rad Mod's CHANGELOG.md history. Decompiling them showed
that assumption was wrong — both gate on the interior-room-crossfade timer (`Sky+0x38`, the same field
`Sky::Update`'s multibound-room logic manages) and operate on `BGSLightingTemplate` (room lighting),
not `TESWeather`. They're the crossfade half of the indoor fog path already found in round 1's
`Sky::UpdateFog`, not weather-to-weather blending. The real purple-sky bug is a different mechanism
(`TESWeather::Aurora`/imagespace-slot inheritance, already documented in Rad Mod's own history) —
recorded the correction in `README-Weather.md` explicitly so a future pass doesn't repeat it.

Also confirmed `Precipitation::QRainDirection`/`QRainDistance`/`QRainOcclusion` are trivial global
static accessors (not per-instance) — genuinely global rain state, though this pass didn't trace where
the underlying globals get WRITTEN, so whether rain direction is actually driven by `UpdateWind`'s
`fWindAngle` remains an open question. `TESRegionDataWeather::LoadRegionDataWeather` confirmed as a
thin thunk to `TESWeatherList::LoadWeatherListChunk`. `Stars::Update` recorded but not synthesized.

## 2026-07-11 — Weather round 3: the general `weatherData[]` decode rule + the full region-weather blend algorithm

6 more functions through `RecordFunctions.java` (98 total). Read the already-decompiled-but-unwritten
backlog first (free value sitting in `DECOMPILE-RECORDS.md` from round 1), then pushed one more level
into `TESRegionDataWeather` (previously completely untouched).

**`TESWeather::GetWeatherData` generalizes round 1's `kTransDelta` finding into the real rule**: EVERY
byte in `weatherData[20]` is a normalized `0-255` value with no inherent unit — `(byte/255) *
(max-min) + min`, with the real-world range supplied by the caller each time. `kTransDelta`'s
formula (round 1) was just one caller of this general accessor, not a special case.

**`TESWeather::GetCloudColor`** confirmed real per-layer color indexing plus previously-undocumented
hardcoded fallback colors (Sunrise `0x9B9B9B`, Sunset `0x1C140F`, Day `0x5B4B46`, Night `0x6C5953`)
used when a weather has zero cloud layers. **`TESWeather::QPrecipitationData`** confirms
`precipitationData` (already a real CommonLibF4 field) is exactly what feeds round 2's
`Precipitation::CreatePrecip` — Rad Mod's original approach was structurally correct throughout.

**`Sky::Update`** confirmed as the real per-frame orchestrator: never writes `currentWeatherPct`
directly (delegates entirely to `UpdateWeather`), and the real sub-update call order is now known:
`UpdateWeather → UpdateFog → UpdateWind → UpdateSound → UpdateAurora → ... → UpdateEffects`. Also
discovered `Sky::Update` has a second, largely separate responsibility — interior room imagespace-LUT
crossfading — not just weather.

**The full region→climate weather-selection chain closed**: `TESRegion::SelectWeather` →
`TESWeatherList::GetRandomWeather` (confirmed identical weighted-roll-with-dynamic-global algorithm to
round 1's `TESClimate::GetRandomWeather` — genuinely shared, not coincidental). **New, previously
totally undocumented finding**: `TESRegionDataWeather::BlendInto` — how two overlapping regions'
weather actually combines. Either the higher-priority region's list wins outright, or (when priorities
are close) a genuine smoothstep-like quadratic blend computes an intermediate priority value via
`TESRegionData::SetPriority` — real, substantive region-overlap behavior with no prior documentation
anywhere in this workspace.

## 2026-07-11 — Weather round 2: added the `Weather` bucket category + decompiled into the real gap classes

Added a proper `Weather` category to `organize_functions.py` (previously `Sky`/`TESWeather`/
`TESClimate`/`Clouds`/`Precipitation`/`Atmosphere`/`Stars`/`Sun`/`Moon`/`SkySound` were scattered
across `Forms_TES`/`Misc_Uncategorized`/`Audio`, exactly the same problem `Quests`/`AI_Combat` had
before those categories existed). Inserted before `Audio` in the rule order so `SkySound` (which
`Audio`'s `contains_any("Sound", ...)` would otherwise claim first) lands correctly. Regenerated:
`Weather.txt` = 362 functions (163), verified clean via a top-class sample (Sky/TESWeather/
Precipitation/Sun/TESClimate/Clouds/TESRegionDataWeather/... — no false positives).

Pushed 8 more functions through `RecordFunctions.java` (92 total), this time into the classes with
**zero** CommonLibF4 coverage rather than just formalizing already-known `Sky::` RVAs:

- **`Clouds::SetBaseTexturesFromWeather`** — confirmed the real `cloudLayerDisabledBits` mechanism:
  a per-layer bit on the weather record gates whether that layer's texture (a `BSFixedString`, 16-byte
  stride starting at `TESWeather+0x28`) gets loaded via `BSShaderManager::GetTexture` or cleared.
- **`Precipitation::CreatePrecip`** — directly explains Rad Mod's failed dusty-precipitation attempt
  (`docs\Rad Mod\PROGRESS.md`: magenta/missing-texture, hard freeze). Confirms the real pipeline is
  genuinely functional (`BGSShaderParticleGeometryData::CreateShaderGeometry`, fed live wind data from
  the `Sky` object, attached to a global `_spWeatherRoot`) — the failure was very likely the specific
  `.BGEM` material used (`CameraAttachWeatherParticlesDusty.BGEM` is a camera-attach effect material,
  not a precipitation one, matching what Rad Mod's own notes already suspected). Worth retrying with a
  correct precipitation-class material rather than avoiding SPGD entirely.
- **`SkySound::Play`/`HandleCheckTime`/ctor** — confirmed why the `ThunderFreq==0` crash (found in
  round 1) was so reliable: normal ambient weather sounds are throttled to a once-per-second recheck,
  but `SoundType == 3` sounds bypass that throttle and re-evaluate on every call — strong evidence
  `SoundType::kThunder` (or equivalent) is value `3`, meaning a bad `ThunderFreq` gets hit continuously
  rather than intermittently.

`Clouds::Update`/`Atmosphere::Update` (main per-frame ticks for two previously-zero-coverage classes)
decompiled and recorded but not yet synthesized. `README-Weather.md` updated with all of the above;
`Precipitation`/`Clouds`/`Atmosphere`/`SkySound` each have at least one real entrypoint documented now,
though most of their ~15-25 functions apiece remain untouched, and `TESRegionDataWeather`/`Stars`
remain essentially unexamined.

## 2026-07-11 — New category Weather: formalized Rad Mod's informal weather RE, plus real new findings

`Projects\Rad Mod\docs\CHANGELOG.md` had a genuine trove of decompile-verified weather RVAs
(`Sky::UpdateWeather`, `Sky::UpdateFog`, `Sky::ForceWeather`, `Sky::UpdateSound`, etc.) from a live
debugging session, but they existed only as changelog prose — never run through this workspace's
formal `decompile_targets.csv` → `DECOMPILE-RECORDS.md` pipeline, and CommonLibF4 has zero header
coverage for `Clouds`/`Precipitation`/`Atmosphere`/`Stars`/`SkySound` despite reserved RTTI/VTABLE
IDs. Ran an Explore survey first to map exactly what was solid vs. gap before touching anything (see
that agent's report in-session), then formalized 20 functions through `RecordFunctions.java`
(category `Weather`, 84 total accumulated across all categories). New doc:
`Organized/1.10.163/README-Weather.md`.

Confirmed and sharpened beyond the informal notes:
- **`Sky::UpdateWeather`'s exact `currentWeatherPct` formula** — `(elapsedHours) /
  (weatherData[kTransDelta]/255 * (transMax-transMin) + transMin)` — previously only described
  qualitatively; this is the formula `RadEngine_Weather.cpp`'s `BeginSmoothWeather()` approximates by
  hand, now available to replicate exactly. Also confirmed a deliberate anti-churn mechanic:
  mid-transition weather changes are probabilistically damped, not always immediate.
- **`Sky::ForceWeather` hard-snap confirmed** by full decompile (not just changelog claim) —
  `currentWeatherPct` is unconditionally set to 1.0, `currentGameHour`/`lastWeatherUpdate` set equal,
  zero gradual-transition code path regardless of the boolean argument.
- **`Sky::UpdateFog`'s complete `fogData[0..17]` index map**, including two Clamp indices (6/7) the
  informal notes missed, PLUS confirmation that fog has three entirely different sources depending on
  context (indoor `BGSLightingTemplate`, per-cell `TESObjectCELL` override, or the weather record
  itself with a real sunrise/sunset-windowed day/night blend and a separate weather-transition blend
  layer) plus a terrain-far-plane fallback override.
- **`TESClimate::GetRandomWeather`'s dynamic-chance mechanic** — a `WeatherEntry`'s effective chance
  can be driven live by a `TESGlobal*` (`chanceVar`) instead of its fixed CK-authored `chance` field,
  re-read on every roll. Not documented anywhere previously.
- **`Sky::UpdateSound`'s `ThunderFreq==0` div-by-zero** formally recorded (was already root-caused
  informally as a real "Dust Storm CTD" bug) — any weather with a Thunder sound needs nonzero
  `ThunderFreq`, full stop.

14 more decompiled-and-recorded functions not yet written up (`Sky::Update`, `UpdateEffects`,
`UpdateWind`, `UpdateAurora`, `UpdateHDRValues`, the Template-blend functions, etc.) — raw C already
in `DECOMPILE-RECORDS.md`, just needs a synthesis pass. Genuine remaining gap: `Clouds`/
`Precipitation`/`Atmosphere`/`Stars`/`SkySound` have no CommonLibF4 coverage at all — every field is
still raw-offset; the raw 1.10.163 dump has ~95 more functions on these classes never touched.

## 2026-07-11 — New tool `SearchStrings.java` + fully decompiled the controller button-prompt glyph system

`PrismaPipboy` needed a verified char→button table for its message-box glyphs (`$Controller_Buttons_
Inverted` font) and was about to build one purely from in-game runtime log capture. `FW_Query.java`
only searches function/symbol NAMES, not string literal contents, so a font name like
`"$Controller_Buttons_Inverted"` was invisible to every search tried. Built
**`Scripts/SearchStrings.java`** — searches Ghidra's defined-string data for a substring and reports
each hit's xrefs (which functions reference it) — a genuinely new capability for this pipeline, not
just another decompile target.

Immediately found `UIUtils::GetButtonHTMLFromUserEvent` (RVA `0x12bea10`) via that string. Decompiled
the full chain: `GetButtonHTMLFromUserEvent` → `ControlMap::GetButtonNameFromUserEvent` (RVA
`0x1b289a0`) → `BSInputDeviceManager::GetButtonNameFromID` (RVA `0x1b2bea0`). Recovered the COMPLETE
button-name→glyph-char table (`Xenon_*` for Xbox, `PSN_*` for PlayStation, `DPad_*` shared) — 33
entries, every `SetAt` call decompiled, not sampled. Full writeup:
`Organized/1.10.163/README-Controllers.md` (new category, general-purpose UI subsystem, not
Pip-Boy-specific).

**Decisive finding, not just a table:** the actual per-call device-resolution logic in
`GetButtonHTMLFromUserEvent` requests `kGamepad` unconditionally whenever *any* gamepad is active
(`BSInputDeviceManager::QUsingGamepad()`), with **zero PlayStation-vs-Xbox branching anywhere in the
call chain**. The `PSN_*` glyph slots are real and populated in the font/table, but native code never
selects them on this runtime regardless of physical controller brand — settles by decompile, not
inference, what `PrismaPipboy` had already suspected from behavior (a PS pad reading as XInput).

## 2026-07-11 — Pipboy coverage sweep: `PipboyWorkshopData`'s real schema, and two stale "unverified" caveats corrected

Follow-up sweep on the new `Pipboy` category, checking every "unverified"/"guessed" claim in
`PrismaPipboy`'s docs against what's actually available.

**`PipboyWorkshopData`'s real key schema, decompiled** (7 key-name static initializers, same
technique already used for `PipboyDataManager`'s root schema): `mapMarkerID`, `owned`, `raider`,
`rating` are true flat fields; `workshopData` is a nested list of generic `{text, value}` rows. This
is a structural correction, not just a naming one — `PrismaPipboy::GameData.cpp::PushWorkshops`'s
existing flat 11-field guess-list (`population, happiness, food, water, ...`) doesn't match the real
shape; those fields are very likely rows *inside* `workshopData`, discoverable generically (same
self-describing pattern as `MiscStatManager`'s stat list), not enumerable flat keys.

**Two "unverified in this fork" claims turned out to be simply false** — CommonLibF4 already has both,
no decompiling required: `RE::QUEST_TYPE` (`QUEST_TYPE.h`, a real 10-value named enum) and
`RE::QUEST_DATA::questType` (`QUEST_DATA.h`, `int8_t` at offset `0x07`). `PrismaPipboy`'s docs had
been citing these as an engine limitation; they're just unread header files. **Corrected in
`docs/Prisma/PrismaPipboy/STATUS.md`** — a reminder that "unverified" claims in project docs are
worth re-checking against current CommonLibF4 headers before treating them as a real RE gap, since
the fork gains coverage over time and old caveats can go stale.

**Checked and genuinely not found:** a native `MARKER_TYPE`→icon-asset mapping. The enum itself
(`MARKER_TYPE.h`, 91 named values) is already complete — what's missing is which icon each value maps
to, and a search for the obvious candidate function names found nothing. Most likely lives in vanilla
`HUDMenu.swf`'s ActionScript, not native code — flagged as out of this pipeline's reach (Scaleform/SWF
decompile is a different toolchain) rather than left as an open native-code search.

## 2026-07-11 — New category: Pipboy — `MiscStatManager` array layout, `PipboyRadioController::SetRadioStation`

A consumer project (`Projects/Prisma/UI_Systems/PrismaPipboy/`) had these two facts only as
paraphrased code comments, not as primary decompile evidence anywhere in this workspace — moved the
actual reverse-engineering here (`Organized/1.10.163/README-Pipboy.md`, category `Pipboy` in
`DECOMPILE-RECORDS.md`) rather than letting engine-internals findings live scattered inside a
project's source comments.

Decompiled `MiscStatManager::AddStatPriv` (RVA `0x1e1660`) and `::QueryStat` (RVA `0x1e1440`) — the
backing store for misc stats, which CommonLibF4 doesn't expose at all. Confirms the global is a real
`BSTArray<MiscStat, BSTArrayHeapAllocator>` (data/capacity/count triple at RVAs `0x58DE838`/`0x840`/
`0x848` — the capacity field wasn't previously distinguished from the count) and that each 0x20-byte
record has **5 fields, not 2**: two `BSFixedString`s (name + an unread second string, built via
stream-append so possibly a category), a value int, an unread `MISC_STAT_T` type-code int, and a third
unread `BSFixedString`. The existing consumer only reads name+value.

Also decompiled `PipboyRadioController::SetRadioStation` (RVA `0xc3f2c0`, previously only cited by
RVA, never opened) — confirms it clamps into the engine's configured frequency range, calls
`RadioManager::TunePlayerRadio`, and pushes the normalized tune position into an `"fRadioTune"`
animation-graph variable on both the player AND the equipped Pip-Boy armor-addon (this is what drives
the in-world tuning dial, not just the audio), playing a static-burst sound cue when the tune value
crosses a quantized station boundary.

## 2026-07-11 — Round 6 (closing round): `TESQuestStageItem::Resolve` — full circle back to round 1

Sixth and final `RecordFunctions.java` batch this session (1 more target, 49 total in
`DECOMPILE-RECORDS.md`). Decompiled `TESQuestStageItem::Resolve` — the per-log-entry action
`TESQuestStage::SetDone` calls, referenced since the very first function opened in round 1 but never
itself decompiled. Confirms the exact mechanism behind three CK per-log-entry checkboxes with their
precise flag bits: bit 0 → `TESQuest::SetCompleted` ("Complete Quest"), bit 1 → `TESQuest::SetFailed`
("Fail Quest"), a separate nonzero byte field → `PlayerCharacter::AddQuestStageItem` +
`TESQuest::SetCurrentJournalText` ("Display in Journal" — what actually populates the Pip-Boy quest
log). Ends by allocating a `StageItemFinishedCallback` object, presumably the bridge to Papyrus's
per-stage script fragment dispatch (not traced further).

**Session summary (rounds 1-6, 49 functions decompiled and documented):** `README-Quests.md` now
covers the full quest lifecycle end-to-end — `SetStage` → `TESQuestStageItem::Resolve` → CK
checkboxes, the `BGSStoryManagerQuestFinder` radiant-quest picker with its complete cooldown/recycle
mechanism, and `BGSBaseAlias` resolution. `README-AI.md` covers the tiered `AIProcess` package
system end-to-end (dispatch → `_Priv`/`_MiddleHigh`/`_High` → schedule rotation → override helpers →
escort resolution), the two-stage `AiFormulas` detection system, and the `CombatBehaviorTree`
execution model in full — `CombatBehaviorThread`-owned state, `Ascend`/`Descend` navigation, a real
leaf action node, and the complete 8-policy child-selector family. Every thread flagged open across
all six rounds has been either resolved or explicitly marked still-open (never silently dropped).
Tooling: `Scripts/RecordFunctions.java`, a reusable batch decompile-and-record tool driven by a
tracked CSV (`Organized/decompile_targets.csv`), now holds 49 real, git-tracked decompiles as a
permanent evidence log for future sessions to build on without re-running Ghidra.

## 2026-07-11 — Round 5: the complete 8-policy child-selector family + the TESNPC package-override helpers

Fifth `RecordFunctions.java` batch (9 more targets, 48 total in `DECOMPILE-RECORDS.md`).

**AI — closed out the child-selector family entirely.** Decompiled the remaining 6 `SelectChild`
policies (`NextChildSelector`, `PriorityChildSelector`, `ValueChildSelector`,
`ConditionalChildSelector`, `IndexChildSelector`, `RandomChoiceChildSelector`), completing the set of
8 alongside `RandomChanceChildSelector`/`WeightedRandomChildSelector` from round 4. Full picture:
sequential, priority-with-a-max-sentinel-short-circuit, value-switch, first-true-conditional,
fixed-index, and three DIFFERENT flavors of randomness — binary coin-flip (`RandomChoice`),
independent per-child chance (`RandomChance`), and magnitude-weighted (`WeightedRandom`). All route
per-child queries through the same vtable slot `0x50` (meaning type-dependent, not itself recovered).
Also decompiled `TESNPC::ComputeEnterCombatOverrideInteruptPackage` (checks a quest-alias-injected
override first, then a base-NPC-form FormList — two independent override sources),
`TESNPC::ComputePlayerOnElevatorOverridePackage` (same shared `ComputePackage` helper, different
FormList), and `TESPackage::GetEscortTargetForPackage` (resolves escort targets through 4 distinct
`PackageTarget` type branches, and can dynamically register the actor as a companion-system follower
via `Actor::AddFollower`, with a real max-follower-count dedup check).

## 2026-07-11 — Round 4: fully resolved the random-child-selection chain (the one genuinely open thread left from round 2)

Fourth `RecordFunctions.java` batch (8 more targets, 39 total in `DECOMPILE-RECORDS.md`), chasing
down the one remaining open question from round 2/3: where does `ChildSelector<
RandomChanceChildSelector>`'s random pick actually happen, given its own `Update`/`Enter` are just
`Ascend()`/the shared no-op.

**Answer: it's not on `ChildSelector` at all — it's on the sibling template
`CombatBehaviorFallbackSelector<Policy>`.** Traced the real chain by decompiling one hop at a time:
`FallbackSelector<RandomChanceChildSelector>::Update` (a real, distinct implementation, found by
dumping ITS vtable separately) → calls a non-virtual, class-specific `::Enter` (not exposed via the
vtable, which is why the earlier vtable-dump-only approach missed it) → which calls
**`RandomChanceChildSelector::SelectChild(node, lastPickedIndex)`** and, on success, **`CombatBehaviorThread::
Descend(thread, childIndex)`** — confirming `Descend` as `Ascend`'s counterpart, completing the
composite-node navigation primitive pair.

`SelectChild`'s search surfaced the FULL selector-policy family — 8 distinct strategies
(`NextChildSelector`, `PriorityChildSelector`, `RandomChanceChildSelector`,
`WeightedRandomChildSelector`, `ValueChildSelector`, `ConditionalChildSelector`,
`IndexChildSelector`, `RandomChoiceChildSelector`). Decompiled the two random ones for a real
comparison: `RandomChanceChildSelector::SelectChild` does INDEPENDENT per-child probability rolls
(each non-excluded child rolls its own `0.0-1.0` chance via a per-child vtable query + `BSRandom::
Float0To1()`; zero, one, or many can pass; one is picked uniformly among the passers) with a
per-instance exclusion bitmask so a picked child isn't re-offered without a reset.
`WeightedRandomChildSelector::SelectChild` is a genuinely different algorithm — true roulette-wheel
weighted selection (sum eligible weights, roll `BSRandom::Float(0, total)`, walk accumulated buckets)
that always picks exactly one child if any are eligible, unlike the chance-based policy.

Also decompiled `BGSStoryTeller::GetIsQuestStarted` (three separate parallel quest-pointer arrays,
presumably active/completed/failed) and `AddWaitingStageForQuest` (a scatter-table-backed waiting-stage
queue), and confirmed `RandomChanceChildSelector`'s own constructor is trivial (a single zeroed
4-byte field — the runtime policy object holds no per-child weight data itself; that lives on the
static tree/child definitions, queried via each child's own vtable slot 0x50).

This closes out every thread flagged open in rounds 1-3.

## 2026-07-11 — Round 3: the radiant-quest cooldown/recycle mechanism, AiFormulas detection-formula structure, a real leaf combat node end-to-end

Third `RecordFunctions.java` batch (7 more targets, 31 total in `DECOMPILE-RECORDS.md`).

**Quests:** decompiled the three `BGSStoryManagerQuestNode` support methods `VisitQuestNode` calls —
`GetQuestHoursUntilReset` (a per-quest-candidate cooldown-override hash table on the node, keyed by
quest-pointer CRC, with a node-wide default fallback), `ShouldRunQuest` (the real per-candidate gate:
already-claimed-by-this-node check, already-started check, cooldown-elapsed check, a "never
auto-reset" quest flag, and a node-level "respect cooldown" flag), and
`ResetRunStateForAllChildren` (recycles off-cooldown candidates back to eligible when a pass exhausts
its fresh pool without a pick). This completes the radiant-quest cooldown/recycling mechanism end to
end.

**AI:** decompiled `AiFormulas::CalculateDetectionFormula`/`CalculateDetectionEventFormula` (both
unqualified in the DB despite being called with the `AiFormulas::` qualifier at their call sites —
Ghidra's decompiler infers a display qualifier from context that isn't present on the stored Function
symbol; had to search for the bare method name). These are dense floating-point formulas built almost
entirely from unnamed tuning constants — confirmed the STRUCTURE (nonlinear `powf()` distance falloff,
a dozen-plus independent boolean toggle terms, two separate output score channels, an explicit
heading-angle re-check) without claiming the exact numeric weights are recoverable. Also decompiled
the real, non-templated `CombatBehaviorAttack::Update` (found via RecordFunctions' qualified-name
fallback match, since a plain substring search on short names alone couldn't find it) — confirmed
target state lives on `CombatBehaviorThread` (not the node), leaf nodes self-schedule via their own
per-instance timer fields compared against a global `AITimer::fTimer`, and `Ascend()` is used
identically by both leaf and composite nodes as the universal "done, return to parent" signal.
**Correction to a round-2 note:** the vtable dump already showed `ChildSelector<
RandomChanceChildSelector>`'s `Enter` slot points at the same RVA as the shared base no-op `Enter` —
so the random-child-pick logic is NOT in an `Enter` override as speculated; still genuinely
unlocated (candidates: the node's own constructor, or an un-found policy method).

## 2026-07-11 — Round 2: found `BGSStoryManagerQuestFinder` (the real radiant-quest picker) and the CombatBehaviorTree's actual execution model

Second `RecordFunctions.java` batch (12 more targets, 25 total accumulated in
`DECOMPILE-RECORDS.md`), searching specifically for the leads the first round left open.

**Quests — the single biggest open question closed:** searching for the `Visit*` method names
directly (rather than `BGSStoryManager::`) surfaced their real owning class:
**`BGSStoryManagerQuestFinder`** — the concrete implementation of `BGSStoryManagerTreeVisitor` that
actually picks radiant quests/events. All three methods decompiled in full: `VisitBranchNode` does
condition-gated sequential OR weighted-random (`BSRandom::Int` + rejection sampling) child traversal;
`VisitQuestNode` is the real cooldown/eligibility filter — memoized via a scatter-table visited-node
cache, checks `BGSStoryTeller::GetIsQuestStarted`, compares `BGSStoryManagerQuestNode::
GetQuestHoursUntilReset` against a per-candidate last-fired timestamp array for the actual "don't
repeat too soon" radiant-quest cooldown, then does weighted-random candidate selection with
condition + `ShouldRunQuest` filtering; `VisitQuest` is the terminal step, calling
`TESQuest::FillAliases` (confirming aliases resolve AT SELECTION TIME) and referencing a global
`TESQuest::pCurrPartialQuest` slot for cross-branch incremental alias resolution. Every decision
point logs through `BGSStoryManager::AddLogEntry` with a distinct code per outcome — the hook point
for whatever surfaces story-manager debug info. Also decompiled `TESQuest::GetAliasIndex` (both
overloads) — confirmed it's a SEPARATE hash table from the runtime alias-binding array
(`DoGetAliasedRef` reads `quest+0xb8`; `GetAliasIndex` validates against the static alias-definition
array at `quest+0x70`), joined by a shared index.

**AI — resolved the `CombatBehaviorTree` "no Evaluate method" dead end from round 1:** dumped
`CombatBehaviorTreeNode`'s full vtable (`FindDumpVtable.java`) and found the base class has no
`Evaluate` slot at all — it's a standard behavior-tree lifecycle (`Enter`/`Exit`/`Update`/`Abort`/
`SaveGame`/`LoadGame`/`Validate`/`GetType`), and the per-tick logic lives in **`Update`**. The same
dump revealed **~160 distinct concrete node types** sharing this vtable (Attack, AcquireOptimalRange,
Bash, Block, CastImmediateSpell, Chase, ChildSelector, Sequence, ...) — a full production behavior
tree, not the simplified boolean-expression system the first pass's `CombatBehaviorExpression<>`
sighting suggested (that's a predicate sub-system used *within* node condition checks, not the tree's
control flow). Decompiled `Sequence::Update` and `ChildSelector<RandomChanceChildSelector>::Update`:
confirmed execution state (current node, per-node child-index counters) lives on
**`CombatBehaviorThread`**, not the tree nodes — meaning the same static tree definition is walked
independently by every actor's own thread, one step per tick (`Ascend()`/descend-into-child), not
re-walked from root every update. Also decompiled `AIProcess::SetCurrentPackageFromSchedule`
(confirmed this is also where companion/follower package assignment via
`Actor::InitiateEscortFollowerPackage` happens, not just generic schedule rotation) and
`CheckforNewPackage_MiddleHigh` (the third tier, between `_Priv` and `_High`).

Both READMEs updated in place. Lesson recorded: don't search for the method name a design pattern
would suggest ("Evaluate") — dump the vtable and read the real slot names.

## 2026-07-11 — New tool: `RecordFunctions.java` (batch decompile-and-record) + closed out the Quests/AI "not yet decompiled" list

Built `ReverseEngineering/Scripts/RecordFunctions.java`: reads a tracked CSV
(`Organized/decompile_targets.csv`, columns `target,category,note`; `target` is `addr:0xRVA`,
`search:substring`, or an exact qualified name) and decompiles every listed function in ONE headless
Ghidra invocation against a single shared `DecompInterface`, writing a persistent, git-tracked raw
evidence log (`Organized/DECOMPILE-RECORDS.md`). This replaces the prior workflow of one
`run_query.ps1` invocation per function (~15-30s JVM+project-load overhead EACH) — the 13-function
batch below completed in one run. To record a new function permanently: add a CSV row, rerun the
script; it regenerates the whole record fresh from the CSV each time (CSV is the source of truth).
Both new files carved out of the blanket `Organized/` gitignore alongside the README-*.md docs.

**Note on running two Ghidra headless queries concurrently:** hit a `LockException` running two
`run_query.ps1` calls in parallel against the same project — Ghidra projects are single-writer.
Serialize all headless invocations against the same project; clear `<project>.lock` between runs if
one was interrupted.

Used the new tool to close out every item the two READMEs below had flagged "not yet decompiled":

- **Quests:** `TESQuestStage::SetDone` (both overloads — corrected an earlier guess that they were a
  wrapper pair; they're independent parallel setters), the three `AcceptVisitor` overrides (confirmed
  the visitor-pattern dispatch mechanism matches CommonLibF4's vtable-slot comments exactly, but the
  actual tree-scoring algorithm lives in a still-unnamed concrete visitor class — genuinely still
  open), and the full `BGSBaseAlias` resolution chain (`GetAliasedRef`/`DoGetAliasedRef`/
  `GetAliasedRefAt` — recovered the 24-byte tagged-union alias slot layout, previously undocumented
  anywhere in CommonLibF4).
- **AI:** `AIProcess::CheckforNewPackage` (confirmed as a tiered dispatcher, not the selector itself)
  plus its `_Priv` and `_High` implementations (recovered the real schedule-time-based package
  rotation algorithm, the door-unlock side effect, and the High-tier-only combat-interrupt-override +
  auto-equip-weapon behavior), and `Actor::CalculateDetectionFormula` (revised the earlier framing —
  detection is two-stage: a faction/combat-gated baseline formula in a separate `AiFormulas`
  namespace, plus a LOS/FOV-gated event-driven refinement from the target's actual detection-trigger
  event). One lead (`CombatBehaviorTreeNode::Func1`, the strongest placeholder-named candidate for the
  behavior tree's `Evaluate`) was decompiled and found to be a dead end — it's the destructor, not
  the evaluator; reported as such rather than silently dropped. The `CombatBehaviorTree` evaluation
  loop itself remains genuinely unlocated.

Both READMEs updated in place with the full synthesis; `DECOMPILE-RECORDS.md` holds the raw C for
all 13 functions.

## 2026-07-11 — Quests + AI subsystems organized and documented (new categories + decompile-verified architecture notes)

`ReverseEngineering/Organized/1.10.163/` had no dedicated Quest bucket (TESQuest/BGSScene/alias/
story-manager functions were scattered, indistinguishable, across `Forms_TES`/`Forms_BGS`/
`Misc_Uncategorized`) and `AI_Combat` missed `AIFormulas`/`AITimer`/`AITimeStamp`/aim-model classes.
Fixed in `Scripts/organize_functions.py`: added a `Quests` category (predicate: `Quest`, `Alias`,
`StoryManager`, `Topic`, `BGSScene`, `SceneAction`, `ScenePhase`, `SCENE_`, `QUEST_` — deliberately
NOT bare `"Scene"`, which would wrongly catch the unrelated 3D `SceneGraph`/`BSSceneGraph` renderer
classes) and widened `AI_Combat`'s predicate to also match any `AI*`-prefixed top-level class plus
`AimModel`/`BGSAimModel`/`TESAIForm`. Regenerated: `Quests.txt` = 6,064 funcs (163), only ~0.8% still
placeholder-named; `AI_Combat.txt` grew to 37,230 (+325, all legitimate new AI-only matches, verified
by sampling). `INDEX.md` now also links two new narrative docs (added a permanent "Deep-dive docs"
section to the generator so it survives regeneration):

- `Organized/1.10.163/README-Quests.md` — `TESQuest::SetStage` decompiled in full (RVA `0x5d71e0`,
  confirms + sharpens `reference_quest_setstage_dormant_esl` memory: exact hash-table stage lookup,
  `TESQuestStage::SetDone` call, dirty-flag vtable call, `TESQuestEvent::Event` broadcast); the
  Papyrus `Quest.SetCurrentStageID` native bridge's exact gate and error string
  ("attempting to start event scoped quest outside of story manager"); confirms
  `TESQuest::EnsureQuestStarted` as the shared auto-start gate used by both the Papyrus bridge and
  `BGSQuestPerkEntry::ApplyPerkEntry`.
- `Organized/1.10.163/README-AI.md` — corrects a natural-but-wrong naming guess (there is no
  `DetectionManager` class; detection math lives on `Actor` itself + `CombatGroup` aggregation).
  `Actor::EvaluatePackage` (RVA `0xd7f980`) decompiled in full: the real AI package re-evaluation
  entrypoint, with a deferred/lightweight path vs. an immediate/full-teardown path that calls
  `AIProcess::CheckforNewPackage` (the real package-picker) — confirms `Actor::EvaluatePackage(true,
  true)` is the correct call for a mod that wants to force an NPC to drop its current behavior and
  re-pick, not manipulating `AIProcess`'s package pointer directly. Also confirms `CombatController`
  evaluates a data-driven `CombatBehaviorTree` (boolean expression nodes over member-function
  predicates) rather than a hardcoded state machine.

Both docs end with an explicit "not yet decompiled" section (e.g. `AIProcess::CheckforNewPackage`'s
own body, the `CombatBehaviorTree` evaluation loop, `BGSStoryManagerTreeVisitor`'s scoring algorithm)
— real open items, not implied-complete coverage.

## 2026-07-11 — HUDMenu lifecycle: NOT safe to kill; UI::CloseAllMenus exempts it; native ShowHUD/CloseHUD found

Investigated whether the vanilla `HUDMenu` Scaleform movie (still resident even though PrismaHUD
patches its 30 widgets' `CanBeVisible` vtable slot to always-false) could be fully unloaded instead
of just widget-hidden, for a real performance win rather than just a rendering-suppression one.

**Verdict: no.** Decompiled `UI::CloseAllMenus` (1.10.163 OG) and found it hardcodes an exemption
list — it walks the menu stack and explicitly SKIPS closing any menu whose name matches `HUDMenu`,
`PowerArmorHUDMenu`, `DebugTextMenu`, `CursorMenu`, or `ButtonBarMenu`. The engine treats these five
as permanent infrastructure even though `HUDMenu`'s own `menuFlags` (set in its ctor, RVA
0x1278970, via `menuFlags |= 0x80C800` = kAllowSaving|kDontHideCursorWhenTopmost|kCustomRendering|
kCompanionAppAllowed) does NOT carry the `kAlwaysOpen` bit itself — the protection is enforced by
the special-cased close-all logic, not the generic flag.

`HUDMenu::HUDMenu` is created via the standard `TMenuFactory<HUDMenu,...>::CreateMenu` factory (same
generic menu-creation pattern as any other menu, confirmed via `FindCallers.java` on the ctor RVA) —
so it's not magically un-recreatable, it's just protected from the bulk sweep.

**Also found (this is what unblocked the actual feature the user wanted):** the engine has its own
native `ShowHUD()` / `CloseHUD()` helpers — `_ShowHUD_SendHUDMessage___AXXZ` (RVA 0xae1f10) and
`_CloseHUD_SendHUDMessage___AXXZ` (RVA 0xae1f80) — that send real `kShow`/`kHide` UIMessages
targeting `"HUDMenu"` via `UIMessageQueue`. These are genuine, Bethesda-authored calls fired at
loading-screen / cutscene / main-menu transitions, going through the same generic menu-stack
open/close pipeline that fires `RE::MenuOpenCloseEvent` for every other menu. So `MenuOpenCloseEvent{
menuName="HUDMenu"}` is a reliable, engine-native signal for "vanilla HUD is going away / coming
back" — no need to reinvent loading/cutscene/main-menu detection.

Also decompiled `HUDMenu::InitializeHUD` (called on every (re)construction): it broadcasts two
global engine events, `PickRefStateChangedEvent` and `HUDReinitializedEvent`, and recomputes HUD
mode state (power armor / chargen / loading / movement-disabled / furniture / vertibird fast-travel
checks) — meaning a full HUDMenu reconstruct is not free and other unknown listeners could depend on
those broadcasts, reinforcing that repeatedly destroy/recreating vanilla HUDMenu itself (as opposed
to our own CEF views) would be the wrong lever.

**Applied:** PrismaHUD_F4 now sinks `MenuOpenCloseEvent` itself (own plugin, no framework changes)
and Destroys (not Hides) its 9 builtin battle-HUD views when HUDMenu closes, recreating them when it
reopens — see `Projects\Prisma\docs\findings\prismahud.md` "2026-07-11: builtin HUD killed (not
hidden)..." for the implementation. `SetHUDColor` (HUDMenu's only public non-ctor method) was
confirmed to have zero external callers via `FindCallers.java`, ruling out one class of "some other
system holds a raw HUDMenu pointer" risk, though not an exhaustive one.

## 2026-07-11 — HUD widget-suppression table completed (all 30 vtable RVAs resolved + verified) + ammo path

**Non-naming RE deliverable** (roadmap item 2, "vanilla ammo readout via GFx"). PrismaUI_F4's
`VanillaUISuppressor` suppresses vanilla HUD widgets by patching each class's `CanBeVisible` vtable
slot (slot 7) to an always-false stub — but its widget RVA table had **20 of 30 entries left at
`0`** (unresolved), so `SuppressHUDWidget("HUDStealthMeter"/"HUDCrosshair"/…)` would silently no-op.

Resolved + verified **all 30** via `Scripts/FindDumpVtable.java` against fwgh_proj_1.10.163_unpacked:
each widget's own primary `VTABLE_<Class>`, with slot 7 confirmed a `CanBeVisible`
(`HUDComponentBase::CanBeVisible` @ RVA `0xa22db0`, or a class override e.g. `HUDCompass::CanBeVisible`
@ `0xa1cad0`, `HUDActionPointMeter::CanBeVisible` @ `0xa03710`, `HUDFatigueWarning::CanBeVisible` @
`0xa34c30`). The 10 pre-existing non-zero RVAs all verified correct. Newly filled (RVA): HUDQuestVaultBoy
`0x2d180f8`, HUDObjectiveUpdates `0x2d18228`, HUDQuestUpdates `0x2d182c8`, HUDTutorialText `0x2d19ee8`,
HUDExperienceMeter `0x2d156f8`, HUDMessages `0x2d167c8`, HUDStealthMeter `0x2d87d18`,
FlashVaultBoyCondition `0x2d1d4c8`, ExplosiveIndicators `0x2d87e28`, DirectionalHitIndicators
`0x2d87ec8`, HUDCrosshair `0x2d150f8`, FlashHitIndicator `0x2d1d348`, HUDLocationText `0x2d87d88`,
HUDSubtitleText `0x2d19ae8`, HUDPerkVaultBoy `0x2d17c58`, HUDCriticalMeter `0x2d14e88`,
HUDFlashLightWidget `0x2d87f38`, HUDPowerArmorLowBatterWarningText `0x2d87c98`, HUDFatigueWarning
`0x2d15a98`, HUDFloatingQuestMarkers `0x2d15cd8`. (For the two Flash-backed widgets — Explosive/
DirectionalHitIndicators — the plain `VTABLE_<Class>` is the HUDComponentBase-derived widget; the
`Flash*` sibling is the Scaleform display object.) Built + deployed the framework.

**Ammo path answered both ways** (the roadmap's "hide the vanilla ammo widget cleanly, or mirror it"):
- *Hide:* `HUDAmmoCounter` (`0x2d13fb8`) + `HUDExplosiveAmmoCounter` (`0x2d14038`) verified suppressable
  (slot 7 = `HUDComponentBase::CanBeVisible`); PrismaHUD already lists both in `kSuppressedWidgets`.
- *Mirror:* not needed — PrismaHUD reads ammo **directly from the engine struct**, casting the equipped
  item to `RE::EquippedWeaponData` (guarded by `EquippedWeaponData::VTABLE[0]`) and reading `wd->ammo`
  + `wd->ammoCount` (`HUDDataProvider.cpp BuildAmmoJSON`). No GFx/Scaleform data-path RE required.

---

## 2026-07-11 — Offset audit of deployed plugins vs the DB: all hardcoded RVAs/offsets verified correct

Swept the active/deployed plugin source for hand-computed RVAs, `REL::Offset{ 0x… }` literals, and
raw struct-offset reads (the compass-alias bug class), then verified each against the now-well-named
1.10.163 Ghidra DB. **All correct — no bugs found.** Verified:

| Plugin / file | Hardcoded value | DB confirms |
|---|---|---|
| `VanillaUISuppressor.cpp` `kActChoiceListenerVtableRVA` | `0x2d21120` | `VTABLE_ActivateChoiceListener` @ `0x142d21120` ✓, slots `[1]=UpdatePickRef [2]=PopulateData [3]=OnButtonRelease` ✓ |
| `VanillaUISuppressor.cpp` ButtonInfo offsets | listener `+0x10`, label `+0x00` | `ActivateChoiceListener::PopulateData` writes listener via `BSTSmartPointer::operator=((out+0x10), listener)`, label at `+0x00` ✓ |
| `VanillaUISuppressor.cpp` `MultiActivateManager::OnButtonRelease` | `0xb7b4d0` | `OnButtonRelease(MultiActivateManager*, BUTTON_INDEX)` ✓ |
| `ModelPreview.cpp` `g_engineGetTexture` | `0x27D61F0` | `GetTexture(char*, bool, NiPointer<NiTexture>*, bool, bool, bool)` ✓ (6-param sig matches) |
| `TerminalBridge.cpp` `GetTerminalMenuItemFragment` | `0x12d8120` | `GetTerminalMenuItemFragment(FragmentSystem*, uint, uint, BSFixedString*, BSFixedString*)` ✓ |
| `TerminalBridge.cpp` `SendTerminalMenuItemRun` | `0x4348a0` | `_SendTerminalMenuItemRun_TESObjectREFR_Events…` ✓ |

Notable: this independently re-confirms the activate-choice quick-loot feature (shipped earlier this
session) is built on correct engine offsets end-to-end — the vtable address, the slot indices, AND
the ButtonInfo struct layout the capture detour reads. Lower-value hardcoded RVAs in test/diagnostic/
experimental code (`MeshSpike` PipboyValue `+0x18`, `CrashDiag`, the `FO4_Wrld` multiplayer spike)
were left unaudited — not deployed on the critical path. **No code changes needed.**

---

## 2026-07-11 — Vtable-shift audit (NEGATIVE result): CommonLibF4 `// NN` ≠ raw vtable slot

**Motivation:** the hknpShape 4-slot shift proved the doodlum RTTI-matcher *can* mis-slot a whole
vtable. Built an audit to find other such shifts systematically: parse CommonLibF4's `// NN` slot
comments from every header (`Scripts` companion parser → `commonlib_vtable_slots.csv`, 158 classes),
then for each class compare `VTABLE_<Class>[slot]`'s function name in the 163 DB against the header's
expected method (`Scripts/VtableAudit.java`).

**Result — the audit does NOT reliably find shifts, and every candidate it flagged was a FALSE
POSITIVE (the DB was already correct).** Root cause: **CommonLibF4's `// NN` numbering does not
consistently equal the raw vtable slot index.** It happened to align for hknpShape (its base
`hkReferencedObject` has a clean 4 slots, so `// 04` == raw slot 4), but for e.g. the `BSTempEffect`
hierarchy CommonLibF4's numbering runs ~3 low (it omits ~3 undocumented base virtuals), so `// 37`
(`ReferenceEffect::Init`) actually lives at raw slot 58. The audit's same-index comparison therefore
misaligns per-class and produces spurious "mismatches."

Verified two of the strongest candidates against the 155 named project (address-keyed = ground truth) — both
DB names are **correct**, my audit was misaligned:
- `ReferenceEffect` slot-58 DB name `Init`; 155 named project `ReferenceEffect::Init` @ `0xc7cdd0` (+0x10 skew
  = 163 `0xc7cde0`). ✓ DB right.
- `PipboyPlayerInfoData` slot-1 DB name `Populate`; 155 named project `PipboyPlayerInfoData::Populate` @
  `0xc3af20` (+0x10 = `0xc3af30`). ✓ DB right.

Also flagged 26 abstract-interface classes (`IMovementState`, `BSISoundCategory`, …) as pure garbage
— they have no standalone `VTABLE_` in the binary, so the lookup grabbed a junk address (every slot
pointed at one `MSVCR110.DLL::GetPackageType` location).

**Action taken: NONE applied** — applying the audit's "fixes" would have *corrupted correct names*
(e.g. renaming `ReferenceEffect::Init` → `Update3D`). hknpShape remains the only confirmed real shift,
already fixed via the community crossref (which compares **address-keyed function identities** — the
reliable method — not `// NN` slot numbers). **Lesson: to find real mis-slotting, compare against
address-keyed sources (155 named project / community IDC labels), NOT CommonLibF4's `// NN` comments.** The
audit tooling (`VtableAudit.java`) is kept but carries this documented limitation.

---

## 2026-07-11 — ALL remaining inconclusive/deferred RE naming resolved (nothing left open)

Closed out every remaining open naming thread from the cross-reference work, using a new
**symbol-vtable-order resolution technique**: for any ambiguous vtable-slot name, dump the class's
vtable from the 1.10.155 named project-named project (`??_7<Class>@@6B@` — full Microsoft private symbols =
ground-truth slot→method order), align by slot index to the 163 vtable, and apply. Both builds put
most vtables at the *same* VA and the per-function addresses differ by a consistent ~0x10 skew, so
alignment is unambiguous. New scripts: `FindDumpVtable.java` (locate + dump any class's vtable),
`CleanRename.java` (force-rename clearing colliding stray labels first).

**hknpShape vtable (was ~20 INCONCLUSIVE "not implemented" stubs):** the 155 named project revealed the names
sitting in the Ghidra project were *shifted by 4 slots* — the whole `[4]..[29]` block was mis-slotted.
Applied the authoritative names to all 26 slots (`0x1414d85f0` = `hknpShape::getType`, `0x1414d8750` =
`getNumberOfSupportVertices`, `0x1414d94d0` = `castRayImpl`, …).

**⚠ IMPORTANT — the error was NOT in CommonLibF4, and this corrects a mischaracterization used loosely
throughout these findings.** Cross-checked three sources at every slot [4]..[19]: **CommonLibF4's own
header (`RE/H/hknpShape.h`, `// NN` slot comments), the 1.10.155 named project vtable, and what got applied — all
three agree perfectly.** CommonLibF4's header is *correct*. The 4-slot shift was introduced by the
**doodlum `BethesdaGhidraScripts` RTTI-matcher** (`name_db_1.10.163_unpacked.py`) when it *applied*
CommonLibF4's method list into the 163 Ghidra DB — it mis-counted the base-class slots and offset the
whole block. The community IDC labels *agreed* with CommonLibF4's header. So the real framing is NOT
"community right, CommonLib wrong" — it's **"CommonLibF4 (header) + community both right; the Ghidra
project's CommonLib-*derived* names were the matcher's shifted misapplication."** General lesson
(reinforcing the 13 misname-pass reverts): the authority is **CommonLibF4's actual header**, not the
CommonLib-derived name sitting in the Ghidra project — always cross-check the header. (Applied names use
the 155/community lowercase Havok convention e.g. `getType`; CommonLibF4's header uses CamelCase
`GetType` — same method/slot, cosmetic-only difference.)

**13 non-Havok INCONCLUSIVE:** 11 were bare/short names → applied the correctly-qualified decoded
name (same method, e.g. `BufAlloc` → `hkLifoAllocator::bufAlloc`, `ShutdownHeapEngine` →
`Scaleform::SysAlloc::shutdownHeapEngine`, `_GetBNetResultString…` →
`BSPlatform::BSBethesdaPlatform::GetBNetResultString`). 2 were genuinely-different-method
disagreements resolved by vtable order: `0x140FACD20` = `Explosion::InitLoadGame` (155 slot [19],
not the "SetCurrentScene" CommonLib claimed), `0x140BA3E20` = `PowerArmorModMenu::ShowCurrent3D`
(155 slot [29], not "RenameCurrent").

**The 2 held reassignments** (from the 202-pass) resolved by direct 155 named project address match (+0x10
skew): `0x140DE5F50` = `EquipActorOutfitItemsFunctor::operator()` (155 @ 0xde5f40),
`0x140705990` = `BGSProcedureTreeConditionalItem::CreateExecState` (155 @ 0x705980). Both decoded
names were right; the earlier INCONCLUSIVE was only because the decompiled bodies were broken
recursive thunks / generic dispatch.

**423 `std::_Func_*` template-formatting deferrals:** swept with `ApplySafeGaps.java` — 0 applicable,
all 423 are un-nameable giant STL functor template instantiations (`std::_Func_impl<lambda…>::_Move`)
Ghidra can't make a namespace from, so the sanitized placeholder is the only representable form.
Bucket definitively closed, nothing to apply.

Net: **41 more addresses authoritatively resolved + applied** (26 hknpShape + 13 non-Havok + 2 held),
verified in the live DB. **Every INCONCLUSIVE / KEEP / deferred item from the entire cross-reference
effort is now either applied or definitively closed — no open RE naming follow-ups remain.**
Key reusable lesson: the 155-symbol-vtable-order technique is the definitive resolver for any
"which method is this vtable slot" question the decompile alone can't answer.

---

## 2026-07-10 — 78 base-class-inheritance reassignments applied (the 202 deferred safe-gap subset)

Decompile-reviewed all 202 engine-class reassignments that the safe-gap pass had deferred (where the
decoded name attributes a `FuncN` vtable slot to a *different* class than the placeholder — the
base-vs-derived / sibling-class pattern I was cautious about). Batch-decompiled + 4 parallel-agent
review against the actual C body. Result: **the caution was unfounded for these** — none were
sibling-class misattributions. Breakdown: ~161 SAME_CLASS_FORMATTING (same class, decoded just adds
the full template / `_anon_XXXXXXXX::` anonymous-namespace spelling), ~38 APPLY_REASSIGNMENT (genuine
inherited base-class methods the derived class's vtable slot points at — confirmed by the body
calling base-class helpers or CRC'ing a literal base-class type-name string), 1 KEEP_PLACEHOLDER, 2
INCONCLUSIVE.

**Applied 199 (reused `ApplySafeGaps.java`): 78 renamed, 121 skipped** as un-nameable giant-template
names Ghidra can't make a namespace from (e.g. `CombatBehaviorTreeValueNodeT<…>`, `NativeFunction2<…>`
— the sanitized placeholder stays as the only representable form). The 78 renamed are the valuable
ones: `MovementControllerNPC::FuncN` → `MovementControllerAI::{UpdateSmallDelta,RegisterInterface,…}`
(7), `NavMeshSearchHide::Func2` → `BSNavmeshSearch::GetNodeConnections`, `BSLightingShaderMaterial::
Func3/4` → `BSLightingShaderMaterialBase::{CopyMembers,ComputeCRC32}`, `Quest/CombatPathingRequest::
FuncN` → base `BSPathingRequest::{Read,Write,CopyTo,GetType}`, `BSScaleformImageLoader::Func3` →
`Scaleform::GFx::ImageCreator::LoadImageFile`, etc. **3 held** (do not re-apply without new evidence):
`0x14067ACA0` (derived-`QueuedHead`-specific strings reject the base name), `0x140DE5F50` +
`0x140705990` (inconclusive — trivial thunk / generic factory dispatch). Held-list detail in
`Organized\safe_gaps_deferred_reassignments.md`.

Verified `MovementControllerAI::UpdateSmallDelta` applied and the prior misname (`MagicItem::Copy`) +
safe-gap (`Begin`) fixes intact. Roadmap item 6 is now fully closed.

---

## 2026-07-10 — 1,959 safe naming-gap fills applied (Class::FuncN placeholders → real names)

Applied the "safe" tier from the 155-vs-163 cross-reference: addresses whose CURRENT name is a
generic CommonLib/Ghidra placeholder (`Class::FuncN`, `_NNN`, `thunk_FUN_...`, empty `Class::`) getting
filled with the 1.10.155-cross-referenced real name. `Scripts\ApplySafeGaps.java`, report in
`Organized\applied_safe_gaps.csv`.

Of the 2,803 safe-gap candidates, first split by whether the decoded name's class MATCHES the
placeholder's class (proactively applying the misname-pass lesson — a *differently*-classed fill is
the same base-vs-derived / sibling-class ambiguity that caused all 13 misname reverts):
- **2,178 same-class fills auto-applied** → **1,954 functions renamed + 5 data statics named**
  (e.g. `BSResourceStreamParser::Func2_v2`→`Begin`, `_50`→`TESForm::AllFormsMapLock`), **219 skipped**
  as un-nameable STL functor template internals (`std::_Func_impl<lambda…>::_Move` — a ~500-char
  template "namespace" Ghidra can't create; zero RE value).
- **625 class-reassignments deferred** (`Organized\safe_gaps_class_reassigned_deferred.csv`): 423 are
  `std::_Func_*` template-formatting-only diffs (ignore), **202 are genuine engine-class reassignments**
  worth a decompile-verified follow-up (documented in `Organized\safe_gaps_deferred_reassignments.md`,
  and roadmap item 6).

Lower confidence than the 137 misname fixes: these are cross-referenced, NOT decompile-verified —
but they overwrite meaningless placeholders (`FuncN`), so even a slightly-off real name is strictly
more informative, and the same-class filter removes the one error mode (wrong class) that actually
matters. Disjoint from the misname set (placeholder-named vs specifically-named), verified intact
(`MagicItem::Copy` misname fix still holds after this pass).

---

## 2026-07-10 — FO4's shared ID3D11Device is NOT created with D3D11_CREATE_DEVICE_SINGLETHREADED

**Question:** is it safe for PrismaUI_F4's ModelPreview.cpp to call `ID3D11Device::CreateBuffer`/etc. from
MULTIPLE background worker threads concurrently on the engine's shared device (needed to parallelize
mesh-load queue depth for PrismaHUD quick-loot's item previews, Bug L)?

**How verified:** decompiled the real call site (not the import thunk) via a new `FindCallers.java` script
(`scripts\FindCallers.java` -- finds+decompiles callers of a given RVA, added this session because no
existing script did "who calls this address"). `D3D11CreateDeviceAndSwapChain`'s import thunk is at RVA
`0x2937fee`; its one real caller is `BSGraphics::CreateD3DAndSwapChain` (`_CreateD3DAndSwapChain_BSGraphics___AXXZ`)
@ RVA `0x1d17460`. The decompiled call:
```c
D3D11CreateDeviceAndSwapChain(local_res18,0,0,0,0,0,7,&local_208,BSGraphics__RendererData__ + 0x70,
                               BSGraphics__RendererData__ + 0x48,local_res10,BSGraphics__RendererData__ + 0x50);
```
Matched positionally against the real `D3D11CreateDeviceAndSwapChain` signature, argument 4 (**Flags**) is
a literal `0`. `D3D11_CREATE_DEVICE_SINGLETHREADED` is bit `0x1` -- not set.

**Result: CONFIRMED safe.** The device defaults to D3D11's standard thread-safe mode (internal driver-level
synchronization on the device object itself, per Microsoft's documented behavior when SINGLETHREADED isn't
requested). Multiple threads may call device-level creation methods (`CreateBuffer`, `CreateTexture2D`,
`CreateShaderResourceView`, ...) concurrently. Does NOT cover the immediate `ID3D11DeviceContext` (draw
calls, `Map`/`Unmap`) -- that remains render-thread-only, unaffected by this finding.

**Used by:** ModelPreview.cpp's worker pool (Bug L fix, `Projects\Prisma\docs\CHANGELOG.md` pt15) --
parallelized the single mesh-load worker thread to 3 threads now that concurrent `CreateBuffer` calls are
confirmed safe. `g_lru`/`g_missing` (previously worker-thread-only, unsynchronized) were given a mutex since
they are NOT safe with >1 worker regardless of the D3D11 finding.

---

## 2026-07-07 — CAVEAT: the original `fwgh_proj\BethesdaGhidraScripts` was built from a PACKED exe

**Discovered while doing unrelated Version Tracking work against a separately-imported 1.10.155 named project
project.** The `Fallout4.exe` copy that `fwgh_proj\BethesdaGhidraScripts` was imported from
(`docs\Knowledge Materials\Reference\BethesdaGhidraScripts\exes\f4\og\Fallout4.exe`, SHA256
`55F57947...`) has a Steam-DRM-wrapped/packed `.text` section, **not real code**. Confirmed via:

- **Entropy:** `.text` reads ~8.0 bits/byte (indistinguishable from random data) across the *entire*
  44MB section, both in the raw exe file and inside the already-imported Ghidra program's own stored
  bytes (real x86-64 code normally reads ~5.5–6.5 bits/byte — see the 1.10.155 named project project for
  comparison, which reads ~6.3). `.rdata` (vtables/RTTI/strings) reads normally (~4.23) in both files.
- **Decompile sanity check:** three functions with "trusted" CommonLib-RTTI names —
  `PlayerCharacter::ServePrisonTime`, `TESObjectREFR::Predestroy`, `PlayerCharacter::GoToPrison` — all
  decompile to nonsense containing `in()`/`out()` (privileged ring-0 port-I/O instructions that cannot
  appear in real user-mode game code) and `halt_baddata()` / "Control flow encountered bad instruction
  data" warnings.

**What this means:** because `.rdata` is intact, the **~14,009 CommonLib-RTTI names and their
addresses are probably still valid** (vtable-slot pointers are metadata, not `.text` bytes). But
**any finding in this log derived from actually reading/decompiling `.text`** — instruction-level
behavior, "how does this function work," anything not sourced from an independent cross-check like a
`static_assert` in CommonLibF4 or CommonLibSSE — **should be treated as unverified** until re-checked
against the corrected project below. Skim each entry's "How verified" line: if it says "cross-checked
against CommonLibSSE/CommonLibF4" it's independent of this bug; if it says "Ghidra decompile" alone,
re-verify it.

**Fix:** a Steamless-unpacked `Fallout4.exe.unpacked.exe` (v1.10.163.0, confirmed via
FileVersion/InternalName, SHA256 `9A147A35...` — a genuinely different file, not a re-copy of either
the packed original or the 1.10.155 retail exe) was imported fresh into
**`E:\F4SE OG\ReverseEngineering\Projects\fwgh_proj_1.10.163_unpacked\`**. Verified fixed: `.text` entropy back to
~6.3 bits/byte, and the same three functions above now decompile to sane C. See `01-SETUP.md` and
`README.md` for the corrected project's details — **use that project going forward**, not the
original `fwgh_proj\BethesdaGhidraScripts` (kept only for reference/comparison, not deleted).

**RESOLVED 2026-07-07 (same day, follow-up):** fully re-verified and re-named. Final state:

- Entropy re-checked directly on `Fallout4.exe.unpacked.exe`: `.text` = **6.334** bits/byte (matches
  1.10.155's 6.33). `PlayerCharacter::ServePrisonTime`/`TESObjectREFR::Predestroy`/
  `PlayerCharacter::GoToPrison` (same three RVAs: `0xeabfe0`, `0x429260`, `0xeabaa0`) re-decompiled
  against the new project: all real, coherent C (Win32 API calls, atomic refcounting, real struct
  field access) — confirmed fixed, not assumed.
- Re-ran `CommonLibImport_F4_OG.py` fresh against the correct bytes: **209,817** total functions,
  **14,824** scoped `Class::Method` names (vs. the old packed project's 199,088 / 14,009 — slightly
  different function count because Ghidra's own disassembly boundaries differ when it's reading real
  code instead of noise).
- Applied a second, independent, version-exact naming source on top: a community-sourced IDC label
  script keyed directly to 1.10.163 addresses (`ReverseEngineering\Symbols\F4_1_10_163_IDCLabels.idc`, name strings
  obfuscated with a **+3 byte-value shift** per character — decoded to
  `ReverseEngineering\Symbols\F4_1_10_163_labels_decoded.csv`, 698,074 `(address, mangled-or-plain name)` pairs, applied
  via `ReverseEngineering\Scripts\ApplyDecodedLabels.java` using Ghidra's own demangler so
  functions get real namespaces/signatures). Result: **657,980** names newly applied (369,895 via
  demangling, 288,085 plain fallback for already-readable strings), **40,073** logged as disagreements
  against pre-existing CommonLib names rather than silently overwritten (see `ApplyDecodedLabels.java`'s
  precedence rule and `01-SETUP.md`'s corrected-project section for the disagreement characterization —
  short version: most are cosmetic format differences or CommonLib `::FuncN` placeholders being
  superseded, not addressing errors, but a real minority — including the two examples in this very
  entry — show CommonLib's original RTTI-vtable-slot matching picked the **wrong slot**: e.g. address
  `0xeabfe0` is genuinely `PlayerCharacter::Revert`, not `ServePrisonTime`; `0x429260` is genuinely
  `TESObjectREFR::`scalar deleting destructor'`, not `Predestroy`. **Any hook/finding elsewhere in this
  repo that references `PlayerCharacter::ServePrisonTime`, `PlayerCharacter::GoToPrison`, or
  `TESObjectREFR::Predestroy` by those names is almost certainly wrong and needs re-deriving from the
  corrected project.**
- Version Tracking (the originally planned technique for cross-version naming) was skipped — the
  direct version-exact label source above already provides far higher coverage than VT correlation
  would have.

---

## 2026-07-07 (pt4) — PrismaHUD compass quest-blip was following the wrong quests: real engine mechanism is `CalculateCompassMarkersFunctor` + `TESQuest::GetActive` ("tracked" bit)

**Symptom (user report, in-game):** compass quest blip felt laggy/jittery and didn't seem to track "the
real" quest — despite the diagnostic log showing `resolved=4` real, successfully-resolved quest target
refs every poll, `questDir` was jumping around a lot (286→302→316→345→6→15→18→17...) over a short
span while walking normally.

**Root cause:** `BuildCompassJSON()` (`PrismaHUD_F4\src\HUDDataProvider.cpp`) walked
`player->objectives` and considered ANY objective with `enstanceState == kDisplayed`, picking whichever
resolved target ref was geometrically nearest. But `kDisplayed` is true for every *active* quest's
current objective, not just the one(s) the player toggled **"Active" (tracked)** in the Pip-Boy quest
log — so with 5 displayed objectives across several quests, the blip was jumping to whichever untracked
side-quest's objective happened to be nearest, re-transitioning (CSS `0.13s` ease) every time a
different one won. That reads as both "wrong quest" and "laggy."

**How found:** per the user's request, searched `ReverseEngineering\Symbols\Fallout4_functions.txt`/`Fallout4_data.txt`
(1.10.155 named project — names/signatures only, addresses don't transfer) for compass/quest-target symbols, then
re-resolved every hit against the real 1.10.163 project (`fwgh_proj_1.10.163_unpacked`) via
`FW_Query.java`/`DecompileAt.java` (a new helper, `ListFuncsInRange.java`, was added to enumerate
functions in an address window when a template's `operator()` doesn't show up under a short-name
substring search). Decompiled the actual vanilla compass pipeline:

- `HUDCompassBase::ProcessCompassMarkerFunctor` (RVA `0xa1be10`) calls `CalculateCompassMarkersFunctor`
  over `PlayerCharacter::questTargets` — a `BSTScatterTableBase<TESQuest*, QuestTargetArray*>` cache at
  `PlayerCharacter+0x7f8` (spinlock at `+0x630`), maintained by the engine itself and shared by other
  consumers too (e.g. `FindNearestObjectiveFunctor`, RVA `0xc5e120`, used elsewhere for "nearest
  objective" lookups).
- `CalculateCompassMarkersFunctor::operator()(TESQuest*, QuestTargetArray*)` (RVA `0xa3c150`) — **the
  key line**: `if (TESQuest::GetActive(*quest)) { ...build markers for this quest's targets... }`. A
  quest that isn't "Active" contributes ZERO compass markers, full stop, regardless of its objectives'
  displayed state.
- `TESQuest::GetActive` (RVA `0x5d6aa0`) decompiles to `(byte)((ushort)*(this+0xf4) >> 0xb) & 1` — bit
  `0x0800` of the quest's `flags` field. This lines up exactly with CommonLibF4's own verified
  `RE::TESQuest` layout (`RE/T/TESQuest.h`): `QUEST_DATA data` at `TESQuest+0xF0`, `QUEST_DATA::flags`
  (`std::uint16_t`) at `+0x4` within that struct → `TESQuest+0xF4`. No raw-RVA patch needed at all —
  this is a plain, already-declared struct field read (`quest->data.flags & 0x0800`), the safest
  possible way to consume this finding (see `feedback_verify_raw_rva_before_shipping` memory for why
  that matters here).
- `TESQuest::SetActive` (RVA `0x5d6ac0`) confirms the same bit is set/cleared symmetrically and fires a
  `TESQuestEvent::Event` notify — this is the exact bit the Pip-Boy's quest-log "Active" toggle
  (`PipboyQuestMenu::ToggleQuestActiveStatus`, found via the symbol table) flips.

**Fix applied:** added `if ((obj->ownerQuest->data.flags & 0x0800) == 0) continue;` to
`BuildCompassJSON()`'s objective loop, skipping any objective whose owning quest isn't Pip-Boy-tracked.
Added a `tracked=N` counter to the throttled diagnostic log alongside the existing
`objectives/displayed/targets/resolved/alias0` counters. Built clean (`xmake`) and redeployed to the
live modlist. **Not yet runtime-confirmed** — per the user's own verification process for the earlier
ammo/compass suppression fix, this needs an in-game screenshot/log check before being marked `[FIXED]`
in `Projects\Prisma\docs\findings\prismahud.md` / `open-items.md`.

**New reusable tool:** `ReverseEngineering\Scripts\ListFuncsInRange.java` — lists every function
whose entry point falls in a given `[startRVA, endRVA)` window, sorted by address. Needed because
`FW_Query.java`'s substring search matches only `Function.getName()` (short name, no class
qualifier) — a templated `SomeClass::operator()` has a short name of just `operator()`, indistinguishable
from every other class's `operator()` in a plain substring search, and there can be hundreds of matches.
Bisecting a known address range (e.g. between a class's known ctor and dtor RVAs) finds the real one
directly.

---

## 2026-07-07 (pt3) — CRITICAL: the pt2 VTable patch shipped with RVAs short by 0x2000000 (32MB) — was patching BGSCreatedObjectManager save/load code, not HUD vtables

**Near-miss, caught before any save was loaded with the broken DLL.** The three vtable RVAs implemented
in `InstallVanillaHUDSuppression()` (pt2 below) were computed by hand from the absolute addresses in the
`VTableDump.java` output (`0x142d13fb8` etc. minus image base `0x140000000`) and the leading `2` was
dropped in transcription: `0xd13fb8` instead of the correct `0x2d13fb8`. All three wrong values landed
inside **`BGSCreatedObjectManager::SaveGame`/`LoadGame`** (the code that serializes/deserializes
player-crafted magic items, enchantments, and potions) — nowhere near any HUD vtable. Since the patch
does an 8-byte function-pointer write (`write_vfunc`) at a hardcoded offset, this was splicing a pointer
value into the middle of live save/load-game **instructions**, run every session at `kGameDataReady`,
before the corruption could ever be observed as a HUD symptom. Caught by re-decompiling what was actually
at the deployed addresses and finding save/load-game code instead of HUD classes — not by symptom, by
the user independently auditing the blast radius before trusting the shipped patch.

**Fix:** corrected the three RVAs to `0x2d13fb8` / `0x2d14038` / `0x2d14528` (the missing leading `2`).
Rebuilt and redeployed immediately, overwriting the broken DLL in the live modlist before any save/load
had exercised the corrupted code path.

**Lesson for any future raw-RVA vtable/function patch derived from an absolute-address minus image-base
subtraction:** hand-computed hex subtraction is exactly the kind of arithmetic a single dropped digit
survives silently (compiles fine, links fine, only breaks at the memory-write target). Before trusting
a hardcoded RVA in a `write_vfunc`/`REL::Offset` call, **re-verify by decompiling/naming the *deployed*
address itself** (not just re-deriving it the same way it was derived the first time) — e.g. run
`VTableDump.java` again against the *specific numeric RVA*, or add a debug-build assertion that reads the
function name at that address and compares it against the expected class before allowing the write.
A silent off-by-32MB is far more dangerous for a raw memory patch than for a REL::ID/address-library
lookup, which at least fails loudly (wrong-version mismatch) rather than landing on unrelated live code.

---

## 2026-07-07 (pt2) — `HUDComponentBase::CanBeVisible` — real vtable slot found; supersedes the SetVariable approach below

**The SetVariable approach in the entry below did NOT work in-game** (confirmed via screenshot: vanilla
ammo/AP still rendered). Root cause: every `HUDComponentBase`-derived widget's own `UpdateDisplayObject()`
re-derives its Scaleform `_visible` member from the virtual `CanBeVisible()` **every engine tick** — far
faster than any poll-based SetVariable re-assertion could win the race. The only way to make a hide
permanent is to change what `CanBeVisible()` itself returns.

**How found:** `CanBeVisible` has NO named entry for `HUDAmmoCounter` in the 1.10.163 project (it doesn't
override the base), so `FW_Query.java`'s name search alone can never find it — it only searches Function
symbols, and an un-overridden virtual has no function-symbol of its own for that class. Wrote a new script,
**`VTableDump.java`** (`ReverseEngineering\Scripts\`, usage: `-postScript VTableDump.java
VTABLE_ClassName`), which dumps a named vtable **Data** symbol's raw function-pointer slots directly —
finds the real backing implementation regardless of whether the owning class overrides it.

**Verified (1.10.163 OG), vtable slot layout for every `HUDComponentBase`-derived class** (slot 0 = dtor
thunk, 7 = `CanBeVisible`):

| Class | Vtable RVA | `CanBeVisible` impl |
|---|---|---|
| `HUDComponentBase` (base default) | `0xd14b28` | `0xa22db0` (own) |
| `HUDAmmoCounter` | `0xd13fb8` | `0xa22db0` (inherited, no override) |
| `HUDExplosiveAmmoCounter` | `0xd14038` | `0xa22db0` (inherited, no override) |
| `HUDCompass` | `0xd14528` | `0xa1cad0` (own override) |

**Full decompiled `HUDComponentBase::CanBeVisible` logic** (RVA `0xa22db0`):

```
bool HUDComponentBase::CanBeVisible(HUDComponentBase* this) {
    if ((this[0xf1] != 0) ||                                  // "force visible" flag, OR
        (this[0xc0] != 0 && appTimer.lastTime < this[0xb8]))  // "temp-show-until" timer still active
    {
        if (IsValidHUDMode(this))                              // reads bool at +0xe4, set by UpdateHUDMode
            if (!player->vtable[0x600](player, 0))              // unnamed player-state gate, slot 192
                return true;
    }
    return false;
}
```

**Used in:** PrismaHUD `InstallVanillaHUDSuppression()` (`HUDDataProvider.cpp`, one-time call from
`main.cpp` kGameDataReady) — `REL::Relocation<uintptr_t>{ REL::Offset{vtableRVA} }.write_vfunc(7,
&AlwaysHidden)` against each of the three vtable RVAs above. Gated behind `REX::FModule::IsRuntimeOG()`
since these are raw RVAs (not REL::IDs), valid for 1.10.163 OG only. This is a ONE-TIME patch, not a
per-poll re-assert — replaces (and is strictly more correct than) the SetVariable approach in the
2026-07-07 (pt1) entry below. **General lesson:** for any vanilla HUD element whose own C++ update loop
re-derives visibility every tick, a GFx `SetVariable` hide will always lose that race — the `CanBeVisible`
vtable slot (or whatever native gate drives the widget) is the only fix that sticks.

---

## 2026-07-07 (pt1) — Full HUDMenu.swf widget path table, straight from a Ghidra decompile (supersedes "live-probe only") [SetVariable suppression built on this table did NOT work — see pt2 above for the real fix]

**Correction to the 2026-06-30 entry below:** the GFx member paths are NOT probe-only after all — every one of
them is a literal argument inside `HUDMenu::HUDMenu`'s constructor, and decompiling that single function gives
the whole table for free, no runtime tree walk needed. `HUDMenu::AddHUDObject<T>(this, "<path>")` is called once
per built-in widget; `apMemberName` **is** the GFx path (relative to `_root`, per the movie AS class glue).

**How:** `run_query.ps1 decompile:HUDMenu` against `fwgh_proj_1.10.163_unpacked` (1.10.163 OG, RVA `0x1278970`).
Full recovered table (all under `_root.`, prefix omitted):

| Widget class | GFx path |
|---|---|
| `HUDQuestVaultBoy` | `HUDNotificationsGroup_mc.QuestVaultBoy_mc` |
| `HUDObjectiveUpdates` | `HUDNotificationsGroup_mc.ObjectiveUpdates_mc` |
| `HUDQuestUpdates` | `HUDNotificationsGroup_mc.QuestUpdates_mc` |
| `HUDTutorialText` | `HUDNotificationsGroup_mc.TutorialText_mc` |
| `HUDExperienceMeter` | `HUDNotificationsGroup_mc.XPMeter_mc` |
| `HUDMessages` | `HUDNotificationsGroup_mc.Messages_mc` |
| `HUDEnemyHealthMeter` | `TopCenterGroup_mc.EnemyHealthMeter_mc` |
| `HUDStealthMeter` | `TopCenterGroup_mc.StealthMeter_mc` |
| `FlashVaultBoyCondition` | `TopRightGroup_mc.VaultBoyCondition_mc` |
| `ExplosiveIndicators` | `CenterGroup_mc.ExplosiveIndicatorBase_mc` |
| `DirectionalHitIndicators` | `CenterGroup_mc.DirectionalHitIndicatorBase_mc` |
| `HUDCrosshair` | `CenterGroup_mc.HUDCrosshair_mc` |
| `HUDRollover` | `CenterGroup_mc.RolloverWidget_mc` |
| `FlashHitIndicator` | `CenterGroup_mc.HitIndicator_mc` |
| `HUDQuickContainer` | `CenterGroup_mc.QuickContainerWidget_mc` |
| `HUDRadiationMeter` | `LeftMeters_mc.RadsMeter_mc` |
| `HUDLocationText` | `LeftMeters_mc.LocationText_mc.RegionText` |
| `HUDPlayerHealthMeter` | `LeftMeters_mc.HPMeter_mc` |
| **`HUDCompass`** | **`BottomCenterGroup_mc.CompassWidget_mc`** |
| `HUDSubtitleText` | `BottomCenterGroup_mc.SubtitleText_mc` |
| `HUDPerkVaultBoy` | `BottomCenterGroup_mc.PerkVaultBoy_mc` |
| `HUDCriticalMeter` | `BottomCenterGroup_mc.CritMeter_mc` |
| `HUDFlashLightWidget` | `RightMeters_mc.FlashLightWidget_mc` |
| `HUDActionPointMeter` | `RightMeters_mc.ActionPointMeter_mc` |
| `HUDActiveEffectsDisplay` | `RightMeters_mc.HUDActiveEffectsWidget_mc` |
| **`HUDAmmoCounter`** | **`RightMeters_mc.AmmoCount_mc`** (cross-validated — matches the 2026-06-30 live-probe result exactly) |
| `HUDExplosiveAmmoCounter` | `RightMeters_mc.ExplosiveAmmoCount_mc` |
| `HUDPowerArmorLowBatterWarningText` | `RightMeters_mc.PowerArmorLowBatteryWarning_mc` |
| `HUDFatigueWarning` | `RightMeters_mc.FatigueWarning_mc` |
| `HUDFloatingQuestMarkers` | `FloatingQuestMarkerBase` |

**Also noted while decompiling:** every `HUDComponentBase`-derived widget's vtable slot named `::Func1` by the
naming pass is just the scalar-deleting-destructor thunk (`~Class()` + optional `MemoryManager::Deallocate`),
**not** `CanBeVisible()`/`UpdateDisplayObject()`/etc. — confirmed by decompiling `HUDComponentBase::Func1` and
`HUDAmmoCounter::Func1` and finding identical dtor-thunk bodies at both. `CanBeVisible()` (a real per-widget
virtual — see `HUDCompass::CanBeVisible`, `HUDActionPointMeter::CanBeVisible`, etc. in the 1.10.155 fully named
symbol dump, `ReverseEngineering\Symbols\Fallout4_functions.txt`) is a much cleaner native suppression hook than SetVariable
(survives any HUDMenu.swf reskin since it's a C++-side gate, not a Scaleform path) but its vtable slot isn't
named in the 1.10.163 project — would need a fresh Ghidra vtable-layout query (walk `VTABLE_HUDCompass` slots)
before it could be safely hooked. Not pursued; the SetVariable approach below was sufficient and lower-risk.

**Used in:** PrismaHUD `HideVanillaCompass()` added 2026-07-07 alongside the existing `HideVanillaAmmo()`
(both now call a shared `HideVanillaHUD(paths)` helper) — `HUDDataProvider.cpp`. Any future PrismaHUD widget that
duplicates a vanilla one (health, AP, rads) can reuse this table with zero further RE.

---

## 2026-06-30 — Vanilla HUDMenu.swf GFx element paths (ammo counters)

**How:** live GFx tree probe (`GFx::Value::VisitMembers`) walking `HUDMenu->uiMovie` `_root`, logged to
`PrismaHUD_F4.log`. These paths are NOT in the binary — they live only in the loaded `HUDMenu.swf`, so a
runtime probe is the only way to get them. Root is `_root`. **Superseded by the 2026-07-07 entry above** — the
paths ARE derivable from the binary after all, straight out of the `HUDMenu::HUDMenu` constructor decompile.

**Vanilla ammo counter path (1.10.163 base HUDMenu.swf, FallUI-HUD disabled):**
- `_root.RightMeters_mc.AmmoCount_mc` — the ammo counter container
  - `.ClipCount_tf` (loaded/mag count text), `.ReserveCount_tf` (reserve text), `.AmmoLineInstance` (the "/" line)
- `_root.RightMeters_mc.ExplosiveAmmoCount_mc` — explosive/grenade counter (`.AvailableCount_tf`, `.TypeIcon_mc`)
- Crosshair lives at `_root.CenterGroup_mc.HUDCrosshair_mc.CrosshairBase_mc.CrosshairClips_mc.*`.

**Used in:** PrismaHUD `HideVanillaAmmo()` — `uiMovie->SetVariable("_root.RightMeters_mc.AmmoCount_mc._visible",
GFx::Value(false))` (+ ExplosiveAmmoCount_mc), re-asserted on the MAIN thread each ammo poll because the engine
re-shows them on weapon draw/holster. GFx must be touched on the main thread (F4SE task queue). Reusable pattern
for hiding/moving any vanilla HUD element: probe the tree once, then SetVariable the `<path>._visible`/`._alpha`.

---

## 2026-06-30 — `TESQuestTarget` layout (the compass alias bug)

**Type:** `RE::TESQuestTarget` (QSTA) — forward-declared only in CommonLibF4 (no field offsets).

**Verified layout (sizeof `0x60`):**

| Offset | Field | Type |
|--------|-------|------|
| `0x00` | `flags` | `REX::EnumSet<Flag, uint8>` (+7 pad) |
| `0x08` | `conditions` | `TESCondition` (one pointer, `0x8`) |
| `0x10` | **`alias`** | `uint32` (the alias ID `GetAliasedRef` wants) |
| `0x14` | pad | `uint32` |
| `0x18` | `teleportPath` | `TeleportPath` |

**How verified:** cross-checked CommonLibSSE's `TESQuestTarget` (RE/T/TESQuest.h — byte-identical engine layout)
against the named FO4 binary, and confirmed FO4 `TESCondition` is exactly `0x8`
(`static_assert(sizeof(TESCondition)==0x8)` in CommonLibF4). `flags(1)+pad(7)+conditions(8)` ⇒ `alias` at `0x10`.

**Bug it caught:** PrismaHUD `HUDDataProvider::BuildCompassJSON` used a `QuestTargetLite { int32 alias@0x00; ... }`
("well-known FO4 layout" — wrong). It read the `flags` byte (0/1) as the alias ⇒ `GetAliasedRef(0/1)` ⇒ wrong or
missing compass quest blip. **Fixed** by moving `alias` to `0x10` with a `static_assert(offsetof==0x10)`. Built + deployed.

**Related confirmed (already in CommonLibF4 headers, used by the same code):**
- `BGSQuestObjective` (sizeof `0x28`): `displayText@0x00, ownerQuest@0x08, targets(TESQuestTarget**)@0x10, numTargets@0x18, index@0x1C, state@0x1E, flags@0x20`.
- `BGSInstancedQuestObjective` (sizeof `0x10`): `objective@0x00, instanceID@0x08, enstanceState@0x0C`.
- `PlayerCharacter::objectives` = `BSTArray<BGSInstancedQuestObjective>` @ `0x7D9` (direct, iterable).
- `PlayerCharacter::questTargets` = `BSTHashMap<TESQuest*, QuestTargetArray*>` @ `0x7F0`.
- `PlayerCharacter::currentMapMarkers` = `BSTArray<ObjectRefHandle>` @ `0x738`.
- `TESQuest::GetAliasedRef(ObjectRefHandle* out, uint32 aliasID)` — `REL::VariantID{ 847223, 2207810 }` (OG id 847223).

---

## 2026-06-30 — Compass HUD classes are NOT exposed in CommonLibF4

`HUDCompass`, `HUDCompassMarker`, `HUDCompassMarkerBase`, `HUDCompassDirectionMarker`,
`PowerArmorHUDCompass` exist in the binary (named via vtable/RTTI) but have **no CommonLibF4 header**. Their
named entries are mostly `::Func1` vtable-slot stubs (dtor/RTTI), not the update math. **Consequence:** a custom
compass (PrismaHUD) should *not* try to bind to `HUDCompass`; instead read `PlayerCharacter`'s quest/marker
members directly and compute the bearing itself (`atan2(dx,dy)` in NavMesh coords, N=0 clockwise).

---

## Naming coverage baseline

After `CommonLibImport_F4_OG.py`: **199,250** total functions, **184,826** still `FUN_`/`sub_`, **14,009** named
`Class::Method`. CommonLib names the documented engine API (RTTI/vtable/REL-derived), not every internal helper.
REL-ID-only functions (e.g. `GetAliasedRef`) and plain structs (e.g. `TESQuestTarget`) are **not** auto-named —
reach them by REL::ID/address or by decompiling a caller.

---

## 2026-07-10 — 137 CommonLib misname bugs fixed in the Ghidra project (decompile-verified, applied)

All 313 candidates from the entry below were decompile-verified: batch-decompiled in one Ghidra
headless run, then independently judged by parallel review agents against the actual C body (calls
made, fields accessed, control flow) rather than the name text. Result: **150 confirmed real
bugs** (CommonLib was confident and wrong — same class as `PlayerCharacter::ServePrisonTime`/
`GoToPrison`), 68 likely MSVC identical-COMDAT-folding artifacts (a newly-documented false-positive
pattern — trivial thunks that can legitimately share an address under two different "correct"
names), 63 cases where the original CommonLib name was actually right (the cross-reference itself
has a real false-positive rate — mostly case/abbreviation differences), 32 inconclusive.

**Applied the same day:** 137 of the 150 renamed live in `Projects\fwgh_proj_1.10.163_unpacked`.
**13 reverted** after a post-apply cross-check against CommonLibF4's own hand-authored headers found
the OLD name was, in every one of those 13 cases, a real member already correctly declared on that
exact class (often with an explicit vtable-slot comment) — CommonLibF4's headers are more
authoritative than a decompile-only read, and the review agents didn't have access to them. Grepped
all 137 applied wrong-name strings against every plugin's C++ source + CommonLibF4 headers: zero
live-code dependencies, so the fix carried no regression risk. Also caught and fixed two real bugs
in the apply tooling itself along the way: Ghidra's demangler `applyTo()` silently no-ops a rename
when the existing symbol has equal/higher source-type priority (only 12/150 actually renamed on the
first attempt despite reporting 150/150 "applied" — fixed by always force-`setName()`ing afterward),
and an idempotency re-check that compared only the trailing method name, not the full class-qualified
name, which produced 12 false "already correct" skips for cases where old and new names happened to
share the same method name on different classes.

Full 150-item list, the applied/reverted breakdown, all four verdict categories, and the
COMDAT-folding + CommonLibF4-header-cross-check methodology notes in
[07-DECOMPILE-CANDIDATES.md](./07-DECOMPILE-CANDIDATES.md).

---

## 2026-07-10 — Subsystem function index + 155-vs-163 disagreement cross-reference

Built a full function export for the 1.10.163 project (365,059 functions — didn't exist as a flat
dump before, only queryable) and bucketed both it and the 1.10.155 named project dump into 19 subsystem
categories for browsability. Then cross-referenced all 40,073 `label_apply_disagreements.csv` rows
(05-LABEL-RECOVERY.md §3) against the independent 1.10.155 named project name set — the same class of check
that already caught `PlayerCharacter::ServePrisonTime`/`GoToPrison` being wrong.

**Result:** 313 candidate misnamed functions (specific CommonLib name, contradicted by an
independent 155 match) and 2,803 safe naming-gap fills (CommonLib placeholder → real name, low
risk). Full method, file locations, and standout candidates (stale `KERNEL32.DLL::*`/MFC-style
`CRectTracker` labels landing on real Scaleform/Havok methods) in
[06-FUNCTION-INDEX.md](./06-FUNCTION-INDEX.md).

**Update:** all 313 candidates were subsequently decompile-verified the same day — see the entry
above and [07-DECOMPILE-CANDIDATES.md](./07-DECOMPILE-CANDIDATES.md) for the confirmed results.

---

## Template for new entries

```
## YYYY-MM-DD — <type or function>
**Verified:** <layout / address / behavior>
**How:** <Ghidra decompile of X / cross-check Y / static_assert Z>
**Used in:** <plugin file:func> — <what changed>
```

---

## 2026-07-29 — Havok behaviour graph variables at runtime (1.10.163 OG)

**Verified:** the symbol index has a complete by-name variable API and live graph access.
Source: f4-re-mcp `lookup_by_name` over `Symbols/1.10.163/Fallout4_163_functions.txt`. Addresses are
RVAs; VA = 0x140000000 + RVA.

| RVA | Symbol | Signature |
|---|---|---|
| `0x81f9e0` | `IAnimationGraphManagerHolder::SetGraphVariableHelper<float>` | `bool(this, BSFixedString*, float*)` |
| `0x81f940` | same, `<int>` | `bool(this, BSFixedString*, int*)` |
| `0x81fbe0` | same, `<bool>` | `bool(this, BSFixedString*, bool*)` |
| `0x81fa80` / `0x81fb30` | same, `<NiPoint3>` / `<NiQuaternion>` | as above |
| `0x81ece0`+ | `BSAnimationGraphManager::ForEachAnimationGraph<AnimationSystemUtils::SetGraphVariableFunctor<T>>` | fan-out to every graph on the holder |
| `0x820790`+ | `AnimationSystemUtils::SetGraphVariableFunctor<T>::operator()` | `ForEachResult(this, BSTSmartPointer<BShkbAnimationGraph>*)` |
| `0x16484d0` | `hkbBehaviorGraph::accessVariableValues` | `hkbVariableValueSet*(this)` |
| `0x164a140` | `hkbBehaviorGraph::getActiveNodes` | `hkArray<hkbNodeInfo*>*(this)` |
| `0x5178f0` | `hkbBehaviorGraphData::getStringData` | `hkbBehaviorGraphStringData*(this)` |
| `0x1627dd0` | `BShkbAnimationGraphHelpers::GetGraphFromRootBehavior` | mangled entry, `BShkbAnimationGraph*(const hkbBehaviorGraph&)` |

**Variables are addressed by name, not index.** Every setter takes a `BSFixedString`, and the engine
resolves it against `hkbBehaviorGraphStringData::variableNames`. So the name written in the behaviour
file is the contract with calling code, and renaming a variable in a graph breaks callers silently.

**`hkVariableTweakingHelper` is present in the retail binary**, constructed from an `hkbBehaviorGraph`
(`0x1f54ed0`), with `getRealVariableIndex(char*)` `0x1f55a40`, `getBoolVariableIndex` `0x1f55940`,
`getIntVariableIndex` `0x1f559c0`, `getVector4VariableIndex` `0x1f55ac0`, `setTweakOn(char*, bool)`
`0x1f55b40`, `updateVariables()` `0x1f55700`. This is Havok's own debug-time variable override class.

**Do not treat this as a working bindingless tweak path.** All that is verified is that the code is
linked into the retail binary. Whether the engine ever constructs one, whether it is reachable from a
plugin, and whether `setTweakOn` has any effect at runtime are all **unknown and unverified**. It needs
a decompile of the constructor at `0x1f54ed0` and its callers before anyone builds on it. Cited here so
the symbols are on record, not as a shortcut.

**Name confidence:** `check_name_confidence("SetGraphVariableHelper")` returns
`unconfirmed_disagreement` for `0x14081D430` and `0x14081D440`. CommonLibF4 calls them
`SetGraphVariableHelper<NiPoint3>` / `<NiQuaternion>`; the decoded label says
`IAnimationGraphManagerHolder::SetGraphVariable(const BSFixedString&, const T&)`, and the decoded form
is `decoded_confirmed_by_155`. Treat the public name as `SetGraphVariable` and both labels as suspect
until decompiled. The typed `<int>`/`<float>`/`<bool>` entries at `0x81f9e0` and friends were not
flagged.

**Not found, still open:** nothing in the index answers where a graph variable is *read* other than
through `hkbVariableBindingSet`. `search_findings("animation graph variable behavior")` returns empty,
so this is the first entry on the subject.

**Used in:** BehaviourGraphStudio (`opencw1/behaviortoolstandalone`) — its Variables tab lists the
names this API takes, and issue #6 there tracks the fact that some variables show no binding yet
visibly drive the graph. `hkbBehaviorGraph::getActiveNodes` plus `accessVariableValues` are the
runtime way to settle that question empirically rather than by static reading.

---

## [CONFIRMED] 2026-08-14 — HitData and ProcessLists are layout-identical between OG 1.10.163 and AE 1.11.221

Asked while scoping OG runtime support for Commonwealth Online: do the struct layouts a
deeply layout-dependent plugin touches actually differ between runtimes? For these two, no.

**HitData** (OG probes, all decompiled via `addrdc:` against `fwgh_proj_1.10.163_unpacked`),
compared against CommonLibF4's AE header offsets — every one matches:

| Member | Offset (both) | OG probe |
|---|---|---|
| `aggressor` | `0x40` | `QAggressor` RVA `0x4665b0` |
| `target` | `0x44` | `QTarget` RVA `0x466620` |
| `weapon` | `0x58` | `QWeapon` RVA `0x466650` |
| `ammo` | `0x80` | `QAmmo` RVA `0x4665c0` |
| `healthDamage` | `0x90` | `QHealthDamage` RVA `0x9dcdd0` |
| `totalDamage` | `0x94` | `QTotalDamage` RVA `0x6638c0` |
| `targetedLimbDamage` | `0x9C` | `QTargetedLimbDamage` RVA `0x9dcf50` |

`HitData::SetAllDamageToZero` (RVA `0xfbf360`) additionally pins `resistedPhysicalDamage` `0xA4`,
`resistedTypedDamage` `0xA8`, `stagger` `0xAC`, `flags` `0xC4`. It writes a qword at `0x90` then a
dword at `0x9C`, bracketing `physicalDamage` at `0x98`.

**ProcessLists** — same result, and the size is pinned:

| Member | Offset (both) | OG probe |
|---|---|---|
| `numberHighActors` | `0x20` | `GetNumberOfActorsInHigh` RVA `0x4de0a0` |
| `highActorHandles` | `0x40` | `QHighProcessArray` RVA `0x12acf0` |
| `allProcesss` | `0xA0` | `QAllProcessArrays` RVA `0x516ec0` |
| `runSchedules` / `runMovement` / `runAnimations` / `updateActorsInPlayerCell` | `0x214`–`0x217` | `QRunSchedules` `0x54ff80`, `QRunMovement` `0x54ff70`, `QRunAnimations` `0x54ff60`, `GetUpdateInPlayerCell` `0x342dc0` |

The four tail booleans landing identically means `sizeof(ProcessLists) == 0x218` on both runtimes.

**Technique worth reusing:** OG carries small named accessors that compile to a single field read
(`QAggressor` is 9 bytes). Decompiling one yields its offset unambiguously, which makes an
OG-vs-AE layout diff cheap. Prefer `addrdc:0xRVA` over `decompile:Class::Method` — the latter
fails for every namespaced name, because Ghidra stores the class as a namespace and matches on the
bare function name.

## [OPEN] 2026-08-14 — Actor::Update's vtable slot index is unverified on OG

CommonLibF4 declares `Actor::Update` at slot `0x0CF` (`RE/A/Actor.h`). That is asserted for AE by
the header; it is **not** confirmed for OG, and this DB cannot currently settle it.

- OG `Actor::Update` = RVA `0x3ec620`, with 10 data references (`RefsTo.java`).
- `VTABLE_Actor` @ `0x142d3f3b8` is a real vtable — RTTI meta pointer `0x143144c60` sits at
  `0x142d3f3b0` — but `Actor::Update` is at **slot 9** of it, and `0x142d3f3b8 - 198*8` lands in
  string data, so it is not the primary table carrying a partial label.
- None of the other data referrers (`0x142d3a7e0`, `0x142d43930`, `0x142d549b0`, `0x142d56860`,
  `0x142c87b30`) has an RTTI meta pointer at `referrer - 0xCF*8 - 8`, so none places `Update` at
  slot `0xCF` from a validated base.
- Slots `[10]`/`[11]` of that table are labelled `TESForm::LoadPartial`/`TESForm::LoadEdit` where
  CommonLibF4 expects `UpdateNoAI`/`UpdateMotionDrivenState` — a reminder that names in this DB
  are label-recovery output and not authoritative (see `05-LABEL-RECOVERY.md`).

**Next step:** locate Actor's primary vtable by RTTI reconstruction
(`Scripts/find_vtables_via_rtti_reconstruction.py`) rather than symbol name, and read the index
directly. Do the same against `fwgh_proj_1.11.221_unpacked` — that project names vtables
`<Class>::vftable_meta_ptr` and carries no `Actor::Update` label, so the AE side is also only
asserted by header, never independently confirmed here.

### [CONFIRMED] 2026-08-14 (addendum) — the OG/AE layout sweep came back clean across 8 structs

Extending the HitData/ProcessLists entry above, all probed via `addrdc:0xRVA` against
`fwgh_proj_1.10.163_unpacked` and compared to CommonLibF4's AE header offsets. **No divergence
found in ~35 offsets.**

| Struct | Member | Offset (both) | OG probe RVA |
|---|---|---|---|
| `AIProcess` | `middleHigh` | `0x08` | `QTorchEvalTimer` `0xe76860` |
| `AIProcess` | `high` | `0x10` | `QPathingCurrentMovementSpeed` `0xe48d90` |
| `AIProcess` | `cachedValues` | `0x58` | `QCachedValues` `0xdef560` |
| `AIProcess` | `trackedDamage` | `0xA0` | `QTrackedDamage` `0xdf0410` |
| `AIProcess` | `editorDeadActive` / `initializingPosition` | `0xE5` / `0xE6` | `0xe2a360` / `0xe34590` |
| `HighProcessData` | `pathingCurrentMovementSpeed` / `knowledgeArray` | `0xC8` / `0x2E8` | via `AIProcess::high` |
| `MiddleHighProcessData` | `torchEvalTimer` | `0x444` | via `AIProcess::middleHigh` |
| `TESNPC` | `attachParents` / `headRelatedData` / `morphWeight` | `0x220` / `0x248` / `0x278` | `0x5d0ae0` / `0x5d0d60` / `0x5d0ee0` |
| `TESNPC` | `crimeFaction` / `numHeadParts` / `tintingData` | `0x2C8` / `0x2E8` / `0x300` | `0x5d0c40` / `0x6941e0` / `0x5d1150` |
| `Actor` | `movementController` / `combatController` | `0x318` / `0x328` | `0xdadbf0` / `0x26aa20` |

`initializingPosition` at `0xE6` being the last member pins `sizeof(AIProcess) == 0xE8` on both.

**`NEW_REFR_DATA` is identical field-for-field**, read out of
`TESDataHandler::CreateReferenceAtLocation` (`0x1140b0`), where the struct arrives in `R8`:
`location` `0x08`, `direction` `0x14`, `object` `0x20`, `interior` `0x28`, `world` `0x30`,
`reference` `0x38`, `addPrimitive` `0x40`, `additionalData` `0x48`, `extra` `0x50` (typed
`BSTSmartPointer<ExtraDataList>`), `forcePersist` `0x6A`. Types match the header at each offset,
not just the numbers.

**Minor, not a divergence:** `AIProcess::QAnimWeaponCulled` (`0xe7a8c0`) reads
`*(byte*)(middleHigh + 0x498) & 1`, where CommonLibF4 names `0x498` `weaponCullCounter` and puts
`animWeaponCull` at `0x49A`. Same region; the header's reading of that byte may be imprecise.

**Still unprobed:** `Actor::currentCombatTarget` `0x380`, `biped` `0x428`, `boolFlags` `0x43C` —
no named OG accessor exists for these, so they need a larger consumer function decompiled.

### [CONFIRMED] 2026-08-14 — Actor vtable slots 0xCF/0xD0/0x128/0x129 are identical on OG and AE (retracts the [OPEN] entry above)

The `[OPEN]` entry above recorded `Actor::Update` as sitting at **slot 9** on OG and therefore
possibly divergent from CommonLibF4's `0x0CF`. **That is withdrawn.** It was chasing a mislabelled
symbol: this Ghidra DB names `0x3ec620` "Actor::Update", but the Address Library index names the
real one `0xd7b020` (og id 534717). `0x3ec620` carries a wrong label — the label-recovery problem
in `05-LABEL-RECOVERY.md`.

OG primary `Actor` vtable recovered by RTTI (`find_vtables_via_rtti_reconstruction.py` on
`Symbols/1.10.163/Fallout4.exe.unpacked.exe`) = VA `0x142d3f3b8`, COL `0x3144c60` — the same
address Ghidra labels `VTABLE_Actor`. Reading its slots straight out of the unpacked exe and
resolving each pointer through the index:

| Slot | OG pointer | Identity (via index) | CommonLibF4 |
|---|---|---|---|
| `0xCF` (207) | `0x140d7b020` | `Actor::Update(float)`, og id 534717 | `Update` — match |
| `0xD0` (208) | `0x140d7c730` | `Actor::UpdateNoAI(float)`, og id 1043635 | `UpdateNoAI` — match |
| `0x128` (296) | `0x140dbd260` | `Actor::ComputeMotionFeedbackSpeedAndDirection`, og id 781482 | match |
| `0x129` (297) | `0x140dbd290` | `Actor::UpdateFeedbackGraphSpeedAndDirection`, og id 515227 | match |

Two consecutive slots (`0xCF`/`0xD0`) landing on `Update`/`UpdateNoAI` is the strong form of the
check — a shifted table could not produce that.

AE confirmed independently rather than trusted from the header: same reconstruction on
`EXE/Fallout4.exe.unpacked.exe` (verified 1.11.221.0) gives the AE primary vtable at
`0x1425602c0`; slot `0xCF` holds `0x140c64510` = Address Library id 2229751, adjacent to
`Actor::UpdateMovement` (2229753).

**Method note.** This DB's *names* are label-recovery output and are not authoritative for
identity questions; several Actor vtable slots carry visibly wrong names
(`TESForm::LoadPartial`/`LoadEdit` where CommonLibF4 expects `UpdateNoAI`/`UpdateMotionDrivenState`).
Use Ghidra for **structure** (offsets, slot contents, decompiles) and the Address Library index for
**identity**. Reading raw vtable bytes out of the unpacked exe and resolving each pointer through
`lookup_by_address` avoids the naming layer entirely — that is what settled this, after
symbol-name-based approaches produced a confident wrong answer.

### [CONFIRMED] 2026-08-14 — Actor member layout is identical on OG and AE (completes the sweep)

Closes the "still unprobed" note in the addendum above. All OG offsets match CommonLibF4's AE
header exactly, with the decompiler's inferred types matching too.

| Member | Offset (both) | OG probe RVA | Type in decompile |
|---|---|---|---|
| `currentProcess` | `0x300` | `Actor::SetCurrentAmmoCount` `0xd8eb40` | `AIProcess*` |
| `actorMover` | `0x308` | `Actor::GetCurrentActorMover` `0xdadfb0` | `ActorMover*` |
| `movementController` | `0x318` | `Actor::QMovementControllerNPC` `0xdadbf0` | `BSTSmartPointer<MovementControllerNPC>` |
| `combatController` | `0x328` | `Actor::QCombatController` `0x26aa20` | `CombatController*` |
| `currentCombatTarget` | `0x380` | `Actor::GetCurrentCombatTarget` `0x4ddf20` | handle dword |
| `biped` | `0x428` | `Actor::GetBiped` `0xd88810` | `BSTSmartPointer<BipedAnim>` |
| `boolFlags` | `0x43C` | `Actor::IsInRandomScene` `0x5854c0`, `SetInRandomScene` `0x596dc0`, `IsMountPointClear` `0xdebea0` | `uint` bitfield |

`boolFlags` is confirmed to bit level, not just offset: `IsMountPointClear` tests bit 2 and
`IsInRandomScene` tests bit 4 of the dword at `0x43C`, matching CommonLibF4's `BOOL_FLAGS`
(`kIsMountPointClear = 1 << 2`, `kInRandomScene = 1 << 4`); `SetInRandomScene` sets/clears `0x10`
at the same offset.

**Probe-hunting note:** `currentProcess` and `actorMover` have no `Q*`/`Get*` accessor of their
own. When no named accessor exists, a small *consumer* works just as well — a function that guards
on the pointer before forwarding (`SetCurrentAmmoCount` null-checks `this + 0x300` then calls
`AIProcess::SetCurrentAmmoCount`) exposes the offset just as unambiguously as an accessor would,
and Ghidra's inferred parameter type corroborates the field type for free.

**Sweep total across the OG/AE layout work: ~45 offsets over 9 structs, zero divergences.**

### [CONFIRMED] 2026-08-14 — OG/AE id pairing coverage, and two AE id disagreements in CommonLibF4

Measured for Commonwealth Online's 90 reachable engine functions by joining CommonLibF4's
`RE/IDs.h` against `IDA_Functions_OG_AE.csv`.

**Join gotcha:** the CSV's `Name` column includes the full signature
(`TESNPC::SetHairColor(BGSColorForm *)`). An exact-name join returns 0 matches and looks like a
total coverage failure. Split on `(` first.

Coverage: **45 / 90 pairable today** — 28 with OG+AE ids both in the CSV, 17 OG-only with the AE
id coming from `IDs.h`. The 45 misses are not spread evenly: **31 are
`BSTEventSource<T>::GetEventSource`** (one repeated singleton-getter shape, solvable once for all
31), 8 are generic bare names that are probably artefacts of name-matching, and 4 are
animation-graph virtuals that dispatch virtually and may need no id. Genuinely unresolved
individual functions: **2** (`AIProcess::GetCommandType`, `TESNPC::GetSex`).

**Two AE id disagreements.** Of the 28 where both sources carry an AE id, 26 agree exactly. These
two do not:

| Function | CommonLibF4 `IDs.h` | `IDA_Functions_OG_AE.csv` |
|---|---|---|
| `Actor::IsPathValid` | 2230279 | 2230273 |
| `Actor::IsPathing` | 2234312 | 2230276 |

One source is wrong for each. Unresolved — worth settling before either id is trusted, and a
reminder that CommonLibF4's generated tables are not automatically authoritative (same lesson as
the mislabelled `Actor::Update` entry above).

### [CONFIRMED] 2026-08-14 — CommonLibF4's `IDs.h` has wrong AE ids for `Actor::IsPathValid` and `Actor::IsPathing`

Resolves the disagreement flagged in the pairing-coverage entry above. **CommonLibF4 is wrong;
`IDA_Functions_OG_AE.csv` is right.**

| Function | CommonLibF4 `IDs.h` | Correct (CSV) | OG id |
|---|---|---|---|
| `Actor::IsPathValid` | 2230279 ❌ | **2230273** | 1522194 (`0xdaf7a0`) |
| `Actor::IsPathingComplete` | 2230274 ✔ | 2230274 | 817283 (`0xdaf7c0`) |
| `Actor::IsPathing` | 2234312 ❌ | **2230276** | 989661 (`0xdaf800`) |

**Evidence — relative layout is invariant across every NG/AE build.** The three predicates are
emitted adjacently by the compiler. Taking `IsPathingComplete` as the anchor (the one id both
sources agree on), measured in 1.10.984, 1.11.137, 1.11.159, 1.11.169, 1.11.191 and 1.11.221:

| Candidate | Gap from anchor, all six builds | OG reference gap |
|---|---|---|
| CSV `IsPathValid` (2230273) | **−0x20** every build | −0x20 ✔ |
| CSV `IsPathing` (2230276) | **+0x40** every build | +0x40 ✔ |
| CLF4 `IsPathValid` (2230279) | +0xA0 — wrong side of the anchor | ✘ |
| CLF4 `IsPathing` (2234312) | ≈+0x126070 — a different code region entirely | ✘ |

OG spacing (`0xdaf7a0` / `0xdaf7c0` / `0xdaf800` = −0x20 / anchor / +0x40) is reproduced exactly by
the CSV ids in all six NG builds. CommonLibF4's two ids reproduce it in none.

Also corroborating: 2230273/74/76 cluster tightly with their neighbours; 2234312 is far outside
the block.

**Note on `IDA_Functions_OG_AE.csv`:** its `AE_Addr` column is **1.11.191**-based, not 1.11.221 —
verified exactly against the 1.11.191 address library. The ids are what carry across; the address
column is build-specific and should not be used against 1.11.221 directly.

**Live impact (not just an OG-support issue).** Both are non-virtual ID-bound inline wrappers in
`RE/A/Actor.h`, so any caller on AE today reaches the wrong engine function. Commonwealth Online
calls all three at `plugin/src/F4TProxyActorController.cpp:858-860` and `:925-927`.

**Not proven by decompile.** The AE Ghidra project carries no names for these and its two program
copies disagree on function boundaries, so identity here rests on the structural argument above
(anchor + invariant spacing + id clustering), not on reading AE code.

---

## [CONFIRMED] Workshop placement — ground snap / object snap / yellow-move patch sites (1.10.163)

Date: 2026-08-15. Project: `fwgh_proj_1.10.163_unpacked`. VA = 0x140000000 + RVA. All RVAs
verified `COUNT 1` in `.text` via SigScan (bytes + call targets confirmed via DumpMany). Context:
PrismaCrafting's "Place In Red" copied psc-87/placeinred's `pat_gsnap`/`pat_osnap`/`pat_Y` verbatim;
all three are **ABSENT** from canonical 1.10.163 (byte-scan = 0 matches) — they target a different
build and had rotted. Re-derived here.

- **Ground snap** is NOT a single JBE. `Workshop::Update` (0x1ef4e0) makes three direct calls on its
  GroundInfo; suppress all three to disable snap:
  - `0x1f190d`  `E8 4E EA 04 00` → CALL `ClampToGround` (0x240360). Returns bool, consumed by
    `MOVZX ESI,AL`, so disable with `30 C0 90 90 90` (XOR AL,AL = "no ground"), not bare NOP.
  - `0x1f188a`  `E8 21 01 05 00` → CALL `CorrectZLocation` (0x2419b0). Result unused → 5× `90`.
  - `0x1f1ccd`  `E8 DE FC 04 00` → CALL `CorrectZLocation` (0x2419b0). Result unused → 5× `90`.
- **Object snap** = `0x1f041c`  `F3 0F 10 35 C8 FA 6E 05` = `MOVSS XMM6,[0x1458dfeec]` (sole xref to
  the snap-query-radius global; XMM6 scales the havok connect-point query box). Disable = overwrite
  8 bytes with `0F 57 F6 0F 1F 44 00 00` (XORPS xmm6,xmm6 + NOP) → zero-size box → no candidates.
- **Yellow / existing-object move** = `0x1f49f3`  `8B 58 14` = `MOV EBX,[RAX+0x14]` (base formID) inside
  `Workshop::WorkshopCanSelectRef` (fn @ 0x1f49d0), the selectability BLACKLIST gate. NOP with
  `0F 1F 00` → blacklist lookup misses → every ref grabbable. NOT the near-identical window in
  `Workshop::ItemCanBeMoved` (~0x20e4fa, a physics-only whitelist).

Unique signatures (COUNT 1) if scanning instead of hardcoding RVA:
- gsnap A: `E8 ? ? ? ? 0F B6 F0 88 85 B8 16 00 00 E9`
- gsnap B: `E8 ? ? ? ? F3 0F 10 05 49 E6 6E 05 41 0F 2E C2 74 0D`
- gsnap C: `E8 ? ? ? ? F3 44 0F 10 44 24 68 80 3D 11 D9 6E 05`
- osnap:   `F3 0F 10 35 ? ? ? ? 33 F6 48 89 85 A0 00 00 00`
- yellow:  `80 79 1A 41 48 0F 44 C1 48 85 C0 74 07 E8 ? ? ? ? EB 07 48 8B 81 E0 00 00 00 8B 58 14` (8B 58 14 @ +27)

---

## [CONFIRMED] Workshop build-menu "too many menus" break is DISPLAY-level, not data-level (1.10.163)

Date: 2026-08-15. Question: does the community "too many workshop menus breaks the build menu" failure live in the WorkshopMenuNode data-tree build (which plugins read) or the Scaleform display?

**Verdict: display-level (Scaleform/SWF). A consumer that reads the RootNode global and renders its own UI BYPASSES the break.** The native C++ path has NO small fixed-array/byte/ushort cap anywhere — data build and Scaleform populate both use dynamic BSTArrays and 32-bit counts.

Evidence (fwgh_proj_1.10.163_unpacked):
- `Workshop::BuildWorkShopMenuNodeTree` @ RVA 0x2122e0 writes the RootNode global **0x58e0850** (`MOVDQA [0x1458e0850],XMM0` @ 0x212376 resets it; `LEA RSI,[0x1458e0850]` @ 0x2124a1). Iterates the full TESDataHandler recipe-FormList array (32-bit count); inserter lambda @ 0x2118e0 always returns 1 (no early-out); node uniqueID is a 32-bit int. Only small constant in the fn is `SUB RSP,0x60`. No cap.
- `StartWorkshop` @ 0x20a3a0 calls the builder (`CALL 0x1402122e0` @ 0x20a58e) on workshop entry, then ShowWorkshopMenu. So RootNode is valid only AFTER workshop-mode entry (rebuilt/reset each time), not on plugin load.
- Scaleform populate re-reads RootNode via `WorkshopMenuNode::VisitSelectedChildren<lambda>` @ 0xbfd670 (loop `while (childIndex < *(uint*)(this+0x20))`, 32-bit); per-item sink `WorkshopMenu::AddLoadTask` @ 0xbf3240 stores ring pos as floats (NiPoint3), dynamic BSTArray::Add. `ProcessMessage` @ 0xbef5e0 GFx Invokes are per-item over a dynamic array. No cap.
- `RenumberWorkshopNodes(ushort)` @ 0x1f89e0 writes depth(node+0x44)/sibling-index(node+0x46) as ushort, per-parent only — wraps at 65536 siblings under one node, not a realistic ceiling.

Root cause of the observed break is therefore above the C++ boundary (WorkshopMenu.swf ActionScript and/or GFx load-task/memory flood) — not decompilable here; needs SWF decompile or in-game A/B to pin the exact SWF threshold, but that does not change the verdict for a CEF consumer.

Consumer impact: PrismaCrafting `SyncWorkshopCategories` (DFS over 0x58e0850, called during its workshop-helper data push AFTER workshop entry) reads the fully-built uncapped tree -> renders every recipe-defined category regardless of the SWF break.

---

## [CONFIRMED] HUD-widget + ActivateChoiceListener vtable Address Library IDs, OG(1.10.163) vs AE(1.11.221)

Date: 2026-08-16. Task: port PrismaUI_F4_NG (kAE = 1.11.221) VanillaUISuppressor, which patches HUD-widget vtables (`SuppressHUDWidget`, `CanBeVisible` slot) and the ActivateChoiceListener vtable (`EnableActivateChoiceFilter`, `PopulateData`/`UpdatePickRef` slots) via `REL::ID(<OG id>)` — single OG ids that assume OG-only. Question: what are the AE ids?

**Method (validated 5/5 on OG):** Ghidra `DumpVtablesFast.java`/`FindDumpVtable.java` on `fwgh_proj_1.10.163_unpacked` (symbol `VTABLE_<Class>`) and `fwgh_proj_1.11.221_unpacked` (symbol `<Class>::vftable`, MSVC RTTI; the paired `<Class>::vftable_meta_ptr` sits 8 bytes earlier) → func-array RVA = symAddr − imagebase → `f4-re-mcp lookup_by_address(rva, version)` → Address Library id. Primary vtable = lowest-address `<Class>::vftable` (MSVC emits primary first; independently corroborated by `Scripts/find_vtables_via_rtti_reconstruction.py`'s 2026-07-17 3/3 match). OG spot-checks 1322923/531327/6492/450198/1246781 all reproduced the CommonLib/FindWidgetID id from the Ghidra RVA.

**Address Library indexes BOTH slots of each vtable** as separate ids: the meta-pointer slot (COL ptr, at vftable−8) and the func-array (vftable). CommonLib `RE::VTABLE::<Class>[0]` / FindWidgetID use the **func-array** id. Confirmed on OG ActivateChoiceListener: meta 0x2d21118→1027057, func-array 0x2d21120→1246781.

**Key result — FO4 Address Library vtable ids are STABLE OG↔AE for ALL 16 HUD widgets + ActivateChoiceListener (17/17)** (same id, different RVA), so the existing single `REL::ID(<OG id>)` already resolves correctly on the kAE runtime for every one. VanillaUISuppressor needs NO change for next-gen. (Contrast: many *functions* got new 2xxxxxx AE ids, e.g. LocalMapRenderer::Render OG 213532→AE 2194685 — hence CommonLib's need for `{og,ae}` pairs there; vtables kept their ids.)

**CAUTION — Ghidra RTTI `::vftable` symbols are misaligned by 8–16 bytes for 4 of these classes in `fwgh_proj_1.11.221_unpacked`** (ActivateChoiceListener, HUDLocationText, HUDFlashLightWidget, HUDPowerArmorLowBatterWarningText). Looking up the AE Address Library id at Ghidra's reported RVA returned null and led to a false "AE gap" conclusion. GROUND TRUTH is the Address Library `.bin` (V0 format: `[u64 count][{u64 id,u64 offset}×count]`) cross-checked against raw bytes: a real MSVC vftable has a `.rdata` COL pointer at offset−8 and `.text` pointers in its slots. All 17 stable ids satisfy that test at their true func-array RVA. Do NOT trust Ghidra `::vftable` placement on the 221 project without this raw-byte confirmation.

Table (Class = OG func RVA → id | true AE func RVA → id; AE RVA = the id's offset in version-1-11-221-0.bin, COL-verified):

| Class | OG func RVA | id | AE func RVA | AE id | Status on kAE |
|---|---|---|---|---|---|
| HUDQuickContainer | 0x2d185a8 | 6492 | 0x252a578 | 6492 | OK (stable) |
| HUDRadiationMeter | 0x2d18d98 | 349393 | 0x252ad18 | 349393 | OK |
| HUDPlayerHealthMeter | 0x2d18018 | 514595 | 0x2529fe8 | 514595 | OK |
| HUDCompass | 0x2d14528 | 1322923 | 0x2526728 | 1322923 | OK |
| HUDSubtitleText | 0x2d19ae8 | 450198 | 0x252ba10 | 450198 | OK |
| HUDPerkVaultBoy | 0x2d17c58 | 1087814 | 0x2529c48 | 1087814 | OK |
| HUDCriticalMeter | 0x2d14e88 | 681516 | 0x2526fe8 | 681516 | OK |
| HUDActionPointMeter | 0x2d136d8 | 531327 | 0x25259a0 | 531327 | OK |
| HUDActiveEffectsDisplay | 0x2d13988 | 410206 | 0x2525c18 | 410206 | OK |
| HUDAmmoCounter | 0x2d13fb8 | 1125794 | 0x2526220 | 1125794 | OK |
| HUDExplosiveAmmoCounter | 0x2d14038 | 1471613 | 0x25262a0 | 1471613 | OK |
| HUDFatigueWarning | 0x2d15a98 | 1500956 | 0x2527b90 | 1500956 | OK |
| HUDFloatingQuestMarkers | 0x2d15cd8 | 1473247 | 0x2527d88 | 1473247 | OK |
| HUDLocationText | 0x2d87d88 | 875306 | 0x25a6258 | 875306 | OK (Ghidra sym 0x25a6248 misaligned −16) |
| HUDFlashLightWidget | 0x2d87f38 | 63640 | 0x25a63f0 | 63640 | OK (Ghidra sym 0x25a63e0 misaligned −16) |
| HUDPowerArmorLowBatterWarningText | 0x2d87c98 | 623296 | 0x25a6170 | 623296 | OK (Ghidra sym 0x25a6160 misaligned −16) |
| ActivateChoiceListener | 0x2d21120 | 1246781 | 0x2532ea8 | 1246781 | OK (Ghidra sym 0x2532e98 misaligned −16) |

**No gaps.** The 4 classes above were initially misread as AE gaps because `f4-re-mcp lookup_by_address` at Ghidra's (wrong) `::vftable` RVA returned null. Parsing version-1-11-221-0.bin directly showed the OG id IS present and stable, and its offset−8 is a `.rdata` COL pointer (verified in the unpacked exe) — i.e. the id points at the TRUE func-array; Ghidra's symbol was 16 bytes early. `REL::ID(<OG id>)` therefore resolves correctly on kAE for all 17 with no `{og,ae}` pairing and no hardcoded RVA. (AE RVAs are for reference only; runtime-specific, never hardcode.)

Prisma-side consumer: `PrismaUI_F4_NG_1.11.221_ISOLATED/src/PrismaUI/VanillaUISuppressor.cpp` (`g_widgetTable`, `kActChoiceListenerVtableID = 1246781`). VERDICT: already next-gen-correct as written — all 17 single `REL::ID(<OG id>)` resolve to the right AE vtable; no code change required. Existing `.rdata`/`.text` guards remain as defence-in-depth.

---

## [CORRECTED 2026-08-17] Local-map + Interface3D helpers ARE in Address Library (OG ids were renumbered)

Date: 2026-08-17. The 2026-08-16 entry below looked up **OG ids** in `version-1-11-221-0.bin` (1155495, 996993, 1020638), got no hit, and concluded "not in addrlib". Reverse-lookup **by AE RVA** finds the new ids. Method: parse V0 `.bin` (`[u64 count][{u64 id,u64 offset}×count]`) + f4-re `address_library` table. Do not trust Ghidra `::vftable` on AE without COL check (unchanged). Do not hardcode a 1.11.191 RVA as 1.11.221.

| Symbol | OG id @ 1.10.163 | AE id | 1.11.221 RVA | Notes |
|---|---|---|---|---|
| `nsPipboy_LocalMap::GetLocalMapCameraExtents` | 1020638 @ 0xb88720 | **2224090** | **0xadb0c0** | 1.11.191 entry of this id was **0xadb250**. On 221, 0xadb250 is mid-function (Ghidra `DecompileAt(0xadb250)` = `FUN_140adb0c0`; prologue `48 8b c4 55 56 41 56` at 0xadb0c0, ret 0xadb440). Same extents math (camera `DAT+0x1a0`, matrix `+0x1f8`). Also present on true-NG 1.10.980/984. |
| Interface3D global renderer list | 996993 @ 0x5a66b58 | **4803727** | 0x326eef0 | AE-only (1.11.137+). Not on 1.10.980/984. |
| `nsPipboy_LocalMap::LocalMapTexture` | 1155495 @ 0x5a67ec0 | **4804207** | 0x327c208 | AE-only (1.11.137+). |
| `LocalMapCamera::SetZoom` | (no id) 0x1dcdd0 | (no id) **0x370d30** | byte-identical `48 8b 41 50 f3 0f 11 48 40 c3`. Ghidra AE: "no function here" (10-byte stub in int3 gap). Do **not** use id 2194683 (0x370d40 on 221 = next fn / OG 945561). |
| `LocalMapRenderer::~LocalMapRenderer` | 1277377 @ 0x1dcf80 | **2194684** | 1.11.221 @ **0x370fb0**. Releases `[this+0x2a8]`, ImageSpaceShaderParam at `+0x210`, camera at `+0x1a0`. Use this + `MemoryManager::Deallocate(ptr, false)` for spike teardown. |
| `nsPipboy_LocalMap::DestroyRenderer` | 312943 @ 0xb88eb0 | **2224096** | 1.11.221 @ **0xadb930**. Locks, dtors **singleton** `pMapRenderer`, dealloc size 0x2b0. Do **not** call from spikes. |
| `LocalMapRenderer::'scalar deleting destructor'` | 229144 @ 0xb8b980 | **UNRESOLVED** | still no AE id; unused now |

Prisma consumer: `REL::ID{1020638,2224090}`, `REL::ID{1155495,0,4804207}`, `REL::ID{996993,0,4803727}`, `REL::Offset{0x1DCDD0,0x370D30}` for SetZoom, spike destroy `REL::ID{1277377,2194684}` + CommonLib `MemoryManager::Deallocate`. GetTexture id 2316549 **is** 0x217a9f0 on 221 (the hang); do not ungated from Present.

## [CONFIRMED] Local-map + Interface3D engine helpers: AE (1.11.221) addresses derived (NOT in Address Library)

Date: 2026-08-16. **SUPERSEDED 2026-08-17** — OG ids are absent because they were **renumbered**; reverse-lookup by AE RVA finds the ids in the table above. The 0xadb250 GetExtents RVA was the 1.11.191 addrlib entry, not the 1.11.221 function start. Original task/method text kept for provenance:

Date: 2026-08-16. Task: enable PrismaUI_F4_NG's local-map (`TextureOverlay`) and Interface3D-renderer paths on kAE, which used single OG-only `REL::ID`s. Finding: unlike vtables, these engine functions/globals are **renumbered AND absent from `version-1-11-221-0.bin`** — the F4SE Address Library simply doesn't cover them, and none is reachable via a vtable. So there is no portable `REL::ID`; the only option is per-runtime verified RVAs (`REL::Offset{ og, ae }`).

Derivation method (no Ghidra project needed for the answers — pure PE analysis + one decompile each): OG functions read from `Symbols/1.10.163/Fallout4.exe.unpacked.exe`, AE from `EXE/Fallout4.exe.unpacked.exe`; addrlib `.bin`s parsed directly (V0). Byte-signature match FAILS across OG→AE (2024 recompile, codegen drift). Anchored instead via structure/decompile:

| Symbol | OG RVA (id) | AE RVA | How derived / verified |
|---|---|---|---|
| `nsPipboy_LocalMap::GetLocalMapCameraExtents` | 0xb88720 (1020638) | **0xadb250** | AE `FUN_140adb250` — identical extents math (camera at pMapRenderer+0x1a0, matrix +0x1f8 offsets 0x160/0x80-0xa8 → 3 NiPoint3). Not addrlib-indexed. |
| Interface3D global renderer list | 0x5a66b58 (996993) | **0x326eef0** | AE `KillSDM`(0xa39f90)/`GetByName`(0xa3a420) decompile: `DAT_14326eef0`=data ptr, +0x10=size — matches OG `{data,cap,size}`. |
| `nsPipboy_LocalMap::LocalMapTexture` (g_localMapTex) | 0x5a67ec0 (1155495) | **0x327c208** | AE `RenderLocalMap`(`FUN_140adbba0`): lock→Render→"LocalMap" atexit→alloc 0x18 BSScaleformExternalTexture→store in `DAT_14327c208`→SetTexture. Structure-identical to OG `RenderLocalMap`@0xb88f50. |
| `LocalMapRenderer::'scalar deleting destructor'` (DeleteRenderer) | 0xb8b980 (229144) | **UNRESOLVED** | No callers (only its `.pdata` entry), no vtable (LocalMapRenderer has none — 0 `.rdata` refs to Render/dtor/ctor), recompiled. Guarded off on AE (renderer kept+reused, freed at exit). |

Wired in `PrismaUI_F4_NG_1.11.221_ISOLATED` as `REL::Offset{ ogRva, aeRva }` (per-runtime; slot order [OG,NG,AE] with trailing-repeat, and the paths are gated to OG||AE so the NG slot is never hit). Build-verified (PrismaUI_F4.dll links). Per-runtime RVAs must be re-verified on future game updates. (Reference RVAs only — the unpacked exe/symbols are never shipped; see the deploy/push guardrails.)

---

## 2026-08-17 — [CONFIRMED] The AnimTextData subgraph id is a composite of two CRC-32 (init-0, no-xorout) hashes

**What.** The 64-bit `AnimationOffsets\<id>.txt` key is computed by the engine from a race record's animation set as

```
id = crc32(join(sapt_paths, "|")) << 32 | crc32(behavior_path)
```

with `crc32` = reflected 0xEDB88320 table, running value init **0**, **no final XOR**, over the
**lowercased** input with backslashes. The low half is the behavior graph (SGNM), the high half is
the set's animation-folder prefixes (SAPT) joined by `|`.

**How verified.**
- Extracted the hash function from the 1.10.163 unpacked exe (symbols-mapped): `0x14afeb0` computes two
  table-driven CRCs (table at RVA `0x2e0a930`, zlib polynomial) with init 0 and a bare `mov [out],esi`
  store — no final XOR. `0x14afc10` builds the high-half input by joining the set's prefix array with
  the separator string `"|"` (RVA `0x2c4c160`). The record constructor stores `lo | hi<<32`.
- The loader (RACE subrecord dispatch, RVA `0x5ff003`) fills SGNM into `[rbp+0xc0]` and appends each
  SAPT string to the prefix pool `[rbp+0xc8]`, which `AddSubGraphData` feeds to the identifier.
- Numeric: **2065 of 3401** shipped base-game subgraph ids recompute **exactly** from `Fallout4.esm`
  race records (the rest come from DLC/mod esms). The crash-log hash `10448007347639226270`
  = `0x90fec753a6b50f9e` splits cleanly: low half `0xa6b50f9e` = crc of
  `actors\character\behaviors\weaponbehavior.hkx`; high half `0x90fec753` = the Automatron
  (`DLCRobot.esm` `DLC01HumanRaceAdditiveSubGraphData`) weapon-pool SAPT join, shared with the three
  arm-injured weapon wrapping behaviors.

**Where it's used.** `symrm crash` (manifest-based resolution) plus the new `symrm hash` command and
`src/Archive/SubgraphHash.cs`, which computes the id from behavior + prefixes directly.

---

## [CONFIRMED] Workshop exit self-destroys the placement item (1.10.163)

**Date:** 2026-08-17. **Runtime:** OG 1.10.163 (`fwgh_proj_1.10.163_unpacked`).

**Question:** On workshop exit, does the engine tear down `Workshop::PlacementItem` / the active
placement reference, or does a mod-created placement ghost survive exit?

**Answer: the engine tears it down.** `Workshop::ProcessExitWorkshop(ContextData&, bool)` @ RVA
**0x217df0** (the real exit handler; the 14-byte `RequestExitWorkshop` @ 0x20a7b0 only sets the
request flag) calls, in order (via `ListCallsIn.java 217df0`):

| site RVA | callee | RVA |
|---|---|---|
| 0x217e08 | `RemovePhysicsForScrappableWorkshopReferences` | 0x217bf0 |
| 0x217e36 | `Workshop::CancelActiveEdit(ContextData&)` | 0x20b6e0 |
| **0x217e5c** | **`Workshop::DestroyCurrentItem(ContextData&, bool)`** | **0x20cce0** |
| 0x217f00 | `Disable` (on a GetSmartPointer'd ref) | 0x1e82a0 |
| 0x217f05 | `DestroySharedWorkshopContainer` | 0x20e1f0 |
| 0x217f70 | `HideWorkshopMenu` | 0xbefd70 |

Key ordering: the **physics purge + `DestroyCurrentItem` run before `HideWorkshopMenu`**, and
`HideWorkshopMenu` is what fires the `MenuOpenCloseEvent(WorkshopMenu, closing)`. So by the time a
mod's menu-close handler runs, the engine has already destroyed the placement item (which, for a
mod ghost created via `InitializePlacementReference`, IS `Workshop::PlacementItem`) on the engine's
own thread, serialized with its own `RemovePhysicsForScrappableWorkshopReferences` collision purge.

**Consequence for PrismaCrafting #45:** a mod does NOT need to destroy its placement ghost on the
workshop-menu-close path — doing so is redundant and, if done synchronously on the menu-event
thread, races the render-thread `Workshop::Update` collision walk (the #54 UAF class). The correct
close-path behavior is to invalidate the placement session and clear the mod's own ref tracking,
and let `ProcessExitWorkshop` do the teardown. (Non-exit force-closes — load/coc — tear down the
cell anyway.)

**Evidence:** `ListCallsIn.java 217df0` on `fwgh_proj_1.10.163_unpacked`, 2026-08-17.
