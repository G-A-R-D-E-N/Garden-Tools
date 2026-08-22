# FO4 plugin foundation design

Date: 2026-08-19

The user supplied a complete spec. This note records the first-pass design that was implemented rather than re-litigating it.

## Scope

Generic lossless plugin reader, FormKey model, inspect CLI, tests, RE inventory. No typed record classes, no writer, no BA2.

## Decisions

- C++23, CMake, GoogleTest, zlib
- Namespace `fo4`
- Exceptions (`parse_error`, `FormIdError`) for malformed input
- `FormKey` is canonical; `RawFormId` is a boundary type
- Separate `resolve_plugin_form_id` (on-disk MAST index) from `resolve_runtime_form_id` (compile index / FE slot)
- Records keep `original_bytes`
- Unknown subrecords are kept
- Real fixtures: small user plugins, not `Fallout4.esm`
