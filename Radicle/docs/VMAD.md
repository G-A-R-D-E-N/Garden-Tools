# Stored VMAD values

## Editing

| Capability | Supported scope |
|---|---|
| READ / EXPOSE | Leading script names, flags and known scalar values; array/struct counts; existing quest fragment/alias views |
| WRITE | Existing leading-script object/alias, string, signed integer, finite float and boolean values; existing elements of type 11 through 15 arrays |
| ADD / REMOVE / CREATE | Leading scripts and their properties (every authorable property type, by index); the VMAD payload itself on the record types xEdit gives one |
| REPLACE | A whole leading property, including its name, flags and type, and its struct members |
| REPLACE | An existing QUST fragment binding: its script name and its fragment (function) name; the stage, stage index, unknown words and the extras script data keep their bytes |
| REMOVE | A leading script, a leading property, or an existing QUST fragment binding for a stage |
| REMAP | Existing complete VMAD grammar, including record-specific tails; unsupported properties fail closed |
| OPAQUE | Variable type 6, nonempty type 16, nested struct members, array/struct element edits, fragment and alias script property values, creating a fragment binding where none exists, fragment tails on PACK, PERK, TERM, SCEN and INFO, unknown versions/formats/tails |

Native `set_vmad_property` addresses the script, property and optional array element by physical index. C ABI `radicle_plugin_set_vmad_*` and C# `PluginFile.SetVmad*` use the same writer. Nullable object targets use the current master context without adding masters. Object padding, unedited values and complete fragment tails survive serialization byte-for-byte. Writer support is limited to versions 5/6 and object formats 1/2.

`set_quest_vmad_fragment` rewrites the script and fragment names of the binding that a quest stage already has, and `remove_quest_vmad_fragment` drops that binding and decrements the fragment count. Both address the binding by stage, refuse a stage that is bound twice or not at all, and leave the fragment script data, the alias list, the other bindings and every byte outside the edited entry alone. The fragment script name, the fragment name and the fragment script's properties are the caller's business: a caller that creates a fragment must also create the script entry the game expects, so this writer never invents one.

`add_vmad`, `add_vmad_script`, `remove_vmad_script`, `add_vmad_property`, `remove_vmad_property` and `replace_vmad_property` (native only) author the script list and the property list. Structure edits fail closed: they refuse an unsupported, malformed, truncated or duplicate VMAD, a payload with trailing bytes, a VMAD version or object format the writer does not support, an index out of range, a property the encoder cannot represent, and a record whose type xEdit gives no VMAD. `add_vmad` writes the empty payload xEdit defaults to (version 6, object format 2, no scripts, `Core/wbDefinitionsFO4.pas:3873-3881`) after the EDID subrecord, which is the position every VMAD record's authoring rule order uses. Struct values are authorable one level deep: a struct member of type 7 or 11 through 17 is refused. `replace_vmad_property` reports `unchanged` and leaves the payload untouched when the replacement encodes to the same bytes. Script and property counts are rewritten from the new list length, so they cannot drift from the entries.

VMAD string values are stored and returned as UTF-8, while names keep the Windows-1252 boundary; lengths may change up to 65535 bytes. C# rejects embedded NUL text at the string marshalling boundary. Wrong types, malformed/duplicate VMAD, invalid local object target types and unparsed data refuse mutation before altering payloads. Missing records/indices report absence. VMAD is dynamically addressed rather than registered as a flat W0 scalar field. Whole-record creation/defaults remain governed by each record tranche.
