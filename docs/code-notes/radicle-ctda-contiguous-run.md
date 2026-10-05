# Radicle CTDA contiguous run

## Radicle/src/schema/ctda.cpp:write_ctda_conditions_in

The replaced run is the first CTDA plus the consecutive CTDA/CIS1/CIS2 subrecords after it; the first other subrecord ends it. Previously every later CTDA extended the erased range, so subrecords between separate condition lists (SCEN phases, PACK, INFO, QUST, PERK) were deleted. A CTDA after the run has ended means the scope holds several lists, so the function returns `multiple_lists` and leaves the record untouched (fail closed). Callers scope the write to one list with a begin/end range.

## Radicle/include/fo4/schema/ctda.hpp:CtdaScopeWrite

`multiple_lists` is a refusal. `write_ctda_conditions` maps it to false; `condition_scopes.cpp:set_record_scoped_conditions` already treats anything other than replaced/inserted as `unchanged`.

## Radicle/include/fo4/plugin/mutation.hpp:set_record_conditions

Calls `write_ctda_conditions_in` directly so a multi-list refusal returns false, while a record with no CTDA list stays a successful no-op.
