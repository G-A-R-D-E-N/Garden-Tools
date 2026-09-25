# HEDR record-count rebuild rule

Radicle recomputes TES4 `HEDR` record count whenever a plugin is rebuilt. A completely clean plugin still returns its preserved source range unchanged, so merely reading and writing a plugin does not normalize its stored count.

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

`write_plugin` follows the same rule only on a dirty/rebuilt plugin:

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
- both committed real fixtures (`FW_HeatherSurvivalPatch.esp`, `SlowTime.esp`) store exactly the recursive count, computed independently of the writer from `collect_records`/`collect_groups`, and a forced rebuild writes the same value back;
- with `FO4_DATA_DIR` set, the same two checks run over every official master present (`Fallout4.esm`, the DLC masters including the WRLD-heavy `DLCCoast.esm`/`DLCNukaWorld.esm`) and every `.esl` in the Data folder (the Creation Club light plugins on a stock install). This is the real ESL/WRLD-heavy acceptance #19 asks for; it skips when the game data is not present, so CI runs only the committed-fixture half.
