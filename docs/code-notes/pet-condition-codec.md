# PET condition codec: typed CTDA parameters

## PluginEditTool/native/src/plugin/ConditionCodec.cpp:describeCondition

Non-GetEventData conditions now render param1 and param2 by declared parameter kind (see `paramKind`). Before this, every nonzero param went through `formIdString`, so a Float such as GetWithinDistance param2 came out as FormKey text, an Integer came out as hex FormKey text, and Alias, QuestStage, Event and enum params came out as FormKey text too. Zero params are still omitted.

- Form: `formIdString` (unchanged).
- Integer: JSON integer (the int32 value).
- Float: JSON number when finite; otherwise the string `0x` plus 8 uppercase hex digits of the bits (see `describeParam`).
- Unknown function name: Form (old behaviour).

Anchors for the declared types: source:xEdit Core/wbDefinitionsFO4.pas:192-248 @93cc0bc5 (TConditionParameterType, ptFloat at 195, ptInteger at 196, ptAlias at 198, ptEvent at 199, ptQuestStage at 201, enums 204-212, record types 215-247); source:xEdit Core/wbDefinitionsFO4.pas:261-741 @93cc0bc5 (wbConditionFunctions rows, for example GetEventData at 554 and GetWithinDistance at 603); source:xEdit Core/wbDefinitionsFO4.pas:5124-5181 @93cc0bc5 (wbConditionParameters: Float is wbFloat, Integer and Alias are itS32, Event, Packdata and QuestStage are itU32, enums are itU32 with a name enum, record types are wbFormIDCkNoReach).

## PluginEditTool/native/src/plugin/ConditionCodec.cpp:paramKind

The declared type comes from `ConditionFunctions::paramsOf` (table `kTable` in `PluginEditTool/native/src/plugin/ConditionFunctions.cpp`). Slots past the declared list are Integer; a function with no declared params therefore renders nonzero params as integers.

- Float maps to Float. Integer, String, Alias, Event, Packdata, QuestStage and the nine enum names map to Integer. Everything else (record types) maps to Form.
- Reference, Actor and Package with the aliases flag (0x02) become Integer; with only the UsePackData flag (0x08) they become Integer (Packdata).
- Exception: slot 0 of GetIsCurrentPackage with Run On 5 stays Form, and only when the aliases flag is set. With only the UsePackData flag, xEdit's else-if still makes the slot Packdata, so that case is Integer.
- Parameter 2 has no exception.

Anchors: source:xEdit Core/wbDefinitionsFO4.pas:808-858 @93cc0bc5 (wbConditionParam1Decider and wbConditionParam2Decider; the aliases branch is at 824-829 and the packdata else-if at 830-831, so the exception only guards the aliases branch); source:xEdit Core/wbDefinitionsFO4.pas:823-831 @93cc0bc5 (the GetIsCurrentPackage Run On 5 exception).

Table check: the 220 functions with a non-None declared parameter type in xEdit (wbDefinitionsFO4.pas:261-741 @93cc0bc5) have the same names as `kTable` in `PluginEditTool/native/src/plugin/ConditionFunctions.cpp:kTable`. The parameter types per name were compared with a script before this change; it was not re-run for this commit. The ConditionFunctions table is reused, not generated from Radicle's ctda_functions.inc (which stores link bits only).

## PluginEditTool/native/src/plugin/ConditionCodec.cpp:buildCondition

Slots are applied after the flags byte is parsed, so `paramKind` sees the final flags. Accepted input per kind:

- Any kind, JSON integer: raw int32 bits, accepted in -2147483648 to 4294967295 (cast to int32); values outside that range are ignored. Note that the old code cast without a range check, so out-of-range values were truncated before.
- Any kind, string `0x` plus 1 to 8 hex digits: raw bits. This is how non-finite floats round-trip.
- Form: FormKey or editor ID first (`resolveConditionFormId`), then a strict whole-string decimal. Order changed from the old decimal-first. A JSON float is ignored.
- Integer: strict decimal first, then FormKey or editor ID. A JSON float with an integral value is accepted; a non-integral one is ignored.
- Float: a JSON number (integer or float) is the float value (5 becomes 5.0f), stored as its bits. A string is strict `strtof` first, then FormKey or editor ID as raw bits (legacy).
- Empty string, null, bool and object leave the param at 0 (as before).
- GetEventData (`is_event_data`) uses the Integer rules for both params, so its raw JSON integers are unchanged.

Behaviour change: a JSON integer on a Float slot is now a value, not raw bits. The old describe never emitted that shape for floats, so no stored round trip breaks. Every other input the old build accepted (integers, decimal strings, FormKey strings, editor IDs) is still accepted. The one ordering change is a decimal string on a Form slot that is also a valid editor ID, which now resolves as the editor ID.

Anchors: source:xEdit Core/wbDefinitionsFO4.pas:554 @93cc0bc5 (GetEventData: ParamType1 ptEvent, ParamType2 ptEventData); source:xEdit Core/wbDefinitionsFO4.pas:860 @93cc0bc5 (wbConditionEventToStr: the event value is `function | member << 16`, so GetEventData params stay raw).

## PluginEditTool/native/src/plugin/ConditionCodec.cpp:describeParam

Non-finite floats render as the string `0x` plus 8 uppercase hex digits of the bits (for example `0x7FC00000`). `buildCondition` reads that string back as raw bits. This keeps NaN and infinity round-trippable without JSON support for them.

## PluginEditTool/native/tests/unit/ConditionCodecTests.cpp

Regression tests for the typed behaviour: "float parameter is a JSON number", "integer parameter round trips as a number", "alias parameter under ParametersUseAliases is an integer", "build accepts the old string forms and JSON integers on float slots", and "non-finite float round trips through 0x hex". On the old code, the first three tests, the non-finite test and the "2.5" and integer 3 cases of the build test fail: the old code returned FormKey text for those slots, and read a JSON integer on a Float slot as raw bits. The GetAngle "7" case already passed.

## PluginEditTool/native/tests/unit/ConditionBackendParamTests.cpp:condition backend: GetConditions serializes the CTDA list

This test was moved out of `PluginEditTool/native/tests/unit/ConditionBackendTests.cpp` (which is over the 750-line guard) into its own file. Its GetAngle param1 assertion now expects the Axis integer 1 instead of the old FormKey text. The GetDistance (Reference) param1 assertion is unchanged.
