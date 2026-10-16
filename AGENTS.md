# Agent rules for GARDEN-TOOLS

## Testing

These rules are permanent. They apply to every agent and every change in this repo.

1. Prefer end-to-end and integration tests that show observable system behavior. Examples are real FO4 plugin data, Papyrus, save/load, streaming, asset pipelines and reference artifacts.
2. Before you implement a fix, write the test from the failure mode you identified.
3. Do not create a new test file for every bug.
4. Extend an existing test suite whenever it fits.
5. Do not write trivial getter, setter, constructor or mock-only tests that have no real regression value.
6. Do not duplicate behavior that existing tests already cover well.
7. Every new test must say which realistic regression it detects.
8. End-to-end tests should produce deterministic evidence where practical: files, hashes, logs or manifests.
9. Keep focused unit tests for important boundary conditions, parsing, serialization, arithmetic and failure handling.
10. Do not add test infrastructure that is bigger or more complex than the feature it tests.

### Where tests go

| Area | Suite |
|---|---|
| Radicle | `Radicle/tests` (one gtest binary, `fo4_tests`) |
| Radicle real-data checks | `*_real.cpp` (runs only on the Fallout 4 data runner) |
| PluginEditTool native | `PluginEditTool/native/tests` |
| PluginEditTool managed | `PluginEditTool/FO4RecordEditor.Tests` and `PluginEditTool/FO4RecordEditor.Core.Tests` |
| BGS | `BGS/tools/tests` |
| PET web | colocated `*.test.ts(x)` files |
