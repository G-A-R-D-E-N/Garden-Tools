# Condition JSON preservation

## PluginEditTool/native/src/plugin/ConditionCodec.cpp:describeCondition / buildCondition

The existing display JSON omits the native padding bytes, unknown scalar slots and CIS strings. Rebuilding that JSON therefore changes even an unchanged condition. A synthetic probe at b6f3e918 reproduces the loss through whole-record and scoped Get/Set/Save operations and compares saved raw CTDA payloads. Each displayed row needs validated native preservation data; rebuild from that seed and apply only supported changed fields. Metadata stays on the row so the existing editor's spread, reorder and stringify operations keep it attached. Preserve unknown codes and comparison bits when unchanged, and refuse unsupported edits rather than guessing their meaning.

## Radicle/src/schema/ctda.cpp:read_ctda_conditions_in

Editable reads must compare the source CTDA/CIS run with the existing serializer before returning values. Payloads that are not the fixed 32-byte CTDA struct (source:xEdit Core/wbDefinitionsFO4.pas:5186-5219 @93cc0bc5) and strings the native model would normalize cannot enter an editable round-trip. GetEventData (source:xEdit Core/wbDefinitionsFO4.pas:554 @93cc0bc5) has no shorter layout; its unchanged rows preserve the 32 bytes and edits are refused. The same shared boundary serves whole-record and scoped reads; additional ownership rules are outside this change.

## PluginEditTool/native/src/plugin/ConditionCodec.cpp:buildCondition

Integer parameters (`param1`, `param2`, JSON integers or integer text) accept -2147483648 through 4294967295. Values in that range are stored as their uint32 bit pattern in the int32 field, so 3000000000 is stored as -1294967296. Values outside it are refused with `<key> <value> is outside [-2147483648, 4294967295]`. Form-ID text still resolves through the existing path when it is not an integer.

## PluginEditTool/native/src/plugin/ConditionCodec.cpp:formIdString / placeholderFormId

A raw that does not resolve in the plugin's master context is written as eight uppercase hex digits and a colon with the full raw value, master byte included (`0A000801:`, `00000000:` for zero). This replaces the earlier six-digit fallback, which dropped the master byte and could not be parsed back to the same raw. `placeholderFormId` accepts exactly that nine-character form, and `resolveConditionFormId` checks it before FormKey parsing and editor-ID resolution. Describe then build round-trips the raw bits, and a zero `compareGlobal` is accepted as raw zero.

## PluginEditTool/native/src/plugin/RadiclePluginConditions.cpp:setConditions / PluginEditTool/native/src/plugin/RadiclePluginMutation.cpp:setConditionsAtPath

An invalid row refuses the whole replacement before staging. Skipping a refused row and replacing the list would delete that row. `ConditionCodec.cpp:buildConditions` validates preservation metadata against unused source rows before building the replacement; canonical but forged bytes and duplicate row identities also refuse. Reordering and deleting existing rows remain supported. New rows use strict field validation, and existing unknown-function or event rows support unchanged preservation only. The C ABI is unchanged.

## PluginEditTool/web/src/ConditionsEditor.tsx:patch / PluginEditTool/native/src/plugin/ConditionCodec.cpp:buildCondition

`_native` carries a byte-integer CTDA array and nullable CIS strings on each row. `_edited` records fields explicitly changed by the editor. Browser `JSON.stringify` normalizes negative zero to zero, so semantic equality alone preserves the original raw bits on an unchanged save; an explicit value edit must override that equality to allow positive zero. Non-finite stored values similarly survive the JSON null projection while explicit non-finite edits refuse. Neither metadata nor edit intent adds native layout assumptions.

## PluginEditTool/native/tests/unit/ConditionPreservationBackendTests.cpp / PluginEditTool/native/tests/unit/ConditionCodecTests.cpp

Synthetic regressions compare saved raw CTDA and CIS payloads through both routes, supported scalar edits and reordering, and no pending edit on malformed or forged preservation data and unsupported rows. They also cover browser zero normalization and explicit positive-zero editing. Native read guards compare raw runs to serializer output and refuse payloads that cannot round-trip. `ConditionCodecTests.cpp` also covers the 32-bit integer range edges and describe-then-build round trips of unresolved form IDs and zero `compareGlobal`. These proofs establish byte preservation, not runtime interpretation of padding fields.

## PluginEditTool/native/src/plugin/ConditionCodec.cpp:buildCondition / PluginEditTool/native/src/plugin/ConditionCodec.cpp:describeCondition

xEdit 93cc0bc5 `Core/wbDefinitionsCommon.pas` `wbConditionRunOnAfterSet` (line 1310) clears the CTDA Reference to 0 when Run On is set to anything other than 2 (Reference, `runOnValue("Reference")`). `Core/wbDefinitionsFO4.pas` lines 5210-5214 make Reference a form ID only for Run On 2; otherwise it is an ignored u32. The codec therefore zeroes the reference when Run On leaves Reference without a reference edit, and refuses a nonzero reference edit under another Run On. Unedited rows keep stale references, which xEdit also round-trips on load. Float-declared parameters, as found through `ConditionFunctions::paramsOf` (GetWithinDistance parameter 2, IsPlayerListening parameter 1), follow `Core/wbDefinitionsFO4.pas` line 5128 (`wbFloat('Float')`): describe emits the stored IEEE float as a JSON number and build stores its 4 bytes. Non-finite stored floats project to JSON null, as the comparison value already does; explicit non-finite edits are refused.
