# Stored VMAD values

## Editing

| Capability | Supported scope |
|---|---|
| READ / EXPOSE | Leading script names, flags and known scalar values; array/struct counts; existing quest fragment/alias views |
| WRITE | Existing leading-script object/alias, string, signed integer, finite float and boolean values; existing elements of type 11 through 15 arrays |
| ADD / REMOVE / CREATE | Refused for scripts, properties, array elements and VMAD payloads; counts and fragment bindings are coupled |
| REMAP | Existing complete VMAD grammar, including record-specific tails; unsupported properties fail closed |
| OPAQUE | Variable type 6, nonempty type 16, recursive struct value authoring, fragment/alias script value authoring, unknown versions/formats/tails |

Native `set_vmad_property` addresses the script, property and optional array element by physical index. C ABI `radicle_plugin_set_vmad_*` and C# `PluginFile.SetVmad*` use the same writer. No mutation changes a property type, name, flags, script list or element count. Nullable object targets use the current master context without adding masters. Object padding, unedited values and complete fragment tails survive serialization byte-for-byte. Writer support is limited to versions 5/6 and object formats 1/2.

VMAD string values are stored and returned as UTF-8, while names keep the Windows-1252 boundary; lengths may change up to 65535 bytes. C# rejects embedded NUL text at the string marshalling boundary. Wrong types, malformed/duplicate VMAD, invalid local object target types and unparsed data refuse mutation before altering payloads. Missing records/indices report absence. VMAD is dynamically addressed rather than registered as a flat W0 scalar field. Whole-record creation/defaults remain governed by each record tranche.
