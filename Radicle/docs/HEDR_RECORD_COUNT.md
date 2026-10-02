# HEDR record-count rebuild rule

Radicle recomputes TES4 `HEDR` record count when TES4 is rebuilt or the record/group counts change. A clean plugin returns its preserved source range unchanged. A mixed write with unchanged structure and preserved TES4 also retains its stored count.

## Proven rule

Oracle: xEdit commit `93cc0bc5a1251936c3c7859eee3150eda12a62d7`, `Core/wbImplementation.pas`.

xEdit's save path is explicit:

1. `TwbMainRecord.GetCountedRecordCount` returns `1`.
2. `TwbContainer.GetCountedRecordCount` recursively sums its children.
3. `TwbGroupRecord.GetCountedRecordCount` returns `1 +` its inherited recursive child count.
4. The file save path computes `RecordCount := GetCountedRecordCount` and writes `HEDR.Elements[1] := Pred(RecordCount)`.

The subtraction removes the TES4 main record itself. Therefore the serialized HEDR count is exactly:

```text
all major records except TES4
+ every GRUP, including nested GRUPs
```

This independently matches the existing real `FW_HeatherSurvivalPatch.esp` observation: three top-level GRUPs plus four non-TES4 major records = HEDR count 7.

## Radicle behavior

`write_plugin` follows the same rule when TES4 is dirty or the structure fingerprint changes:

- recursively count every top-level and nested `Group` as one;
- count every group child `Record` as one;
- patch bytes 4..7 of the 12-byte HEDR payload;
- reject duplicate HEDR, missing HEDR, wrong-width HEDR, or counts exceeding u32 storage;
- do not alter clean plugins whose `plugin_original_bytes(plugin)` span is still non-empty.

The HEDR patch is applied to a TES4 copy during serialization, so the const input `Plugin` object is not mutated by `write_plugin`.

## Tests

`tests/test_hedr_count.cpp` covers both sides of the contract:

- a clean plugin with a deliberately stale stored count remains byte-identical;
- a dirty plugin containing a top-level group, major record, nested group, and nested major record rebuilds HEDR to `4`;
- both committed real fixtures (`FW_HeatherSurvivalPatch.esp`, `SlowTime.esp`) store exactly the recursive count, computed independently of the writer from `collect_records`/`collect_groups`; after deliberately corrupting their in-memory HEDR counts, a forced rebuild restores that value;
- with `FO4_DATA_DIR` set, every official master present (`Fallout4.esm`, the DLC masters including the WRLD-heavy `DLCCoast.esm`/`DLCNukaWorld.esm`) and every `.esl` in Data is rebuilt and its emitted HEDR is checked against the independently computed recursive count. Stored and recursive counts are recorded separately as test properties. Stored metadata need not already be canonical. The test skips when game data is absent.

`tests/test_hedr_count.cpp:OfficialMastersAndLightPluginsRebuildTheRecursiveCountWhenPresent` was checked against Steam build 24564252 (Fallout 4 1.11.240). Its `Fallout4.esm` stores 1,741,853, while an independent streaming scan and Radicle both count 1,549,276 non-TES4 records plus 112,381 groups, totaling 1,661,657. `DLCCoast.esm` and `DLCNukaWorld.esm` store their recursive totals, 320,320 and 282,868. These observations do not establish why the base master's stored count differs. The rebuild oracle remains xEdit's `TwbFile.PrepareSave`, not the input's stored value.
