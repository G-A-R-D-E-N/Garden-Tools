# Review rules for GardenTools

These come from what the team fixed before merge in recent pull requests. Report a problem in these areas whenever the code shows it. They never replace judging the code itself.

## Tests
- A test must be able to fail. Report assertions that accept skip, fail or unverified as a pass, `GTEST_SKIP` that hides a real regression, expectations that are always true, and tests that pass against the old code too.
- New branches, refusals and error paths need a test. Say which realistic regression the missing test would catch. Do not ask for getter, setter or mock-only tests.
- Real-data tests run only on the Fallout 4 data runner (`*_real.cpp`). Do not ask for them in the unit suites. BGS is a frozen reference with no test lane: never ask for tests there.

## Failure, cancel and skip paths
- A run that was cancelled, partly skipped, or could not read its input must not report pass, complete or absent. Report any status that hides the first failure, tallies an error as "absent", or reads an empty file as "nothing found".
- Report cleanup that is missed on failure, cancel or shutdown (scratch folders, worker threads that never start, runs left marked active, stamps or markers written before the work succeeds).
- Report a silent fallback that logs nothing.

## Safety
- Path guards must survive `\\?\` and `//?/` prefixes and `..`. Report any read or write outside the MCP path policy or the data-folder guard.
- Text from a pull request or a plugin that is posted to GitHub or put into a command must be escaped or scrubbed. Report raw Markdown, unscrubbed user or machine names, and values echoed into workflow commands.

## paths: PluginEditTool/web/**
- Async UI: report results dropped or overwritten when the user edits, switches tabs or panels, or starts another run while a save or run is in flight. Report a failed status probe that disables the panel until reload.

## paths: Radicle/**
- A new C API wrapper must follow the `FLAT_WRAPPERS_FREEZE` policy, and the ABI number in the code notes must match the header.
- Round-trip checks must compare fields, not only length. Save paths must check the expected source hash for every stage.
- Plugin edits that add masters must keep load order. Report checks that over-refuse real plugins.
- Python tools in `Radicle/tools` that parse with regexes must not skip unmatched rows silently.

## paths: docs/**, wiki/**, re-requests/**, **/docs/code-notes/**
- Notes must match the code and each other: cited files, symbols, ABI numbers and counts must exist and agree. Report notes that claim changes the diff does not make, sections that contradict each other, and links to files that do not exist.
- Explanations belong in `docs/code-notes`, not in source comments. A source comment must not cite a deleted or renamed note.

## Scope
- Report unrelated edits in a pull request, such as duplicated declarations or notes for other changes.
