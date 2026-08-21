# HEDR record-count rebuild rule

Radicle recomputes TES4 `HEDR` record count whenever a plugin is rebuilt. A completely clean plugin still returns its preserved `original_bytes` unchanged, so merely reading and writing a plugin does not normalize its stored count.

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
- do not alter clean plugins whose `original_bytes` are still present.

The HEDR patch is applied to a TES4 copy during serialization, so the const input `Plugin` object is not mutated by `write_plugin`.

## Tests

`tests/test_hedr_count.cpp` covers both sides of the contract:

- a clean plugin with a deliberately stale stored count remains byte-identical;
- a dirty plugin containing a top-level group, major record, nested group, and nested major record rebuilds HEDR to `4`.

Issue #19 still asks for a third real ESL/WRLD-heavy sample as an additional fixture-level cross-check. That sample remains useful, but the binary writer rule itself is no longer inferred solely from the existing fixtures.
