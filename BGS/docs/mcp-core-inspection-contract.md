# BGS core inspection service contract

## Purpose

Phase 1 needs one provider/transport-neutral service between existing BGS domain code and model-facing hosts.

`AssistantInspection` belongs in `BehaviourGraph.Core`. It has no dependency on MCP, `Microsoft.Extensions.AI`, Avalonia, HTTP, JSON-RPC, or a model provider.

Recommended placement:

```text
BGS/src/Hkx/AssistantInspection.cs
BGS/src/Hkx/AssistantInspectionModels.cs
```

`BehaviourGraph.Core.csproj` already compiles `Hkx/*.cs`, so this stays inside the existing core boundary without changing compile globs.

Do not add MCP/provider packages to the core project.

## Service surface

```text
AssistantInspection
  InspectBehavior(...)
  ResolveProjectChain(...)
  CheckProject(...)
  SearchProject(...)
  InspectAnimation(...)
  InspectObject(...)
```

Each method composes existing BGS behavior. No alternate HKX parser/project resolver/validator is introduced.

## Common result semantics

Every result exposes:

```text
source
status
code
message
warnings[]
truncated
```

For this disk-backed core service:

```text
source = disk
```

A later editor-aware adapter may return `source = editor`; do not make disk inspection pretend to represent unsaved state.

Status:

```text
ok
partial
error
```

Initial stable codes:

```text
ok
invalid_argument
path_not_allowed
path_link_not_allowed
path_not_found
path_not_file
unsupported_file
unreadable_file
unsupported_hkx
object_not_found
project_not_resolved
project_scan_failed
internal_failure
```

Domain findings are not transport errors. A graph can return `status = ok` while containing validator errors because inspection itself succeeded.

Never expose stack traces, exception type names, credentials, environment values, or unrelated filesystem data.

## Exact bounds

```text
DefaultListLimit = 50
HardListLimit = 200
MaxDisplayString = 500
```

Rules:

- omitted limit -> 50
- `limit <= 0` -> `invalid_argument`
- `limit > 200` -> effective limit 200
- if a caller requested >200, add a bounded warning that the limit was clamped
- `truncated = true` only when valid result items were actually omitted by the effective limit
- never use `truncated` merely because the requested number was clamped when fewer than 200 items exist
- no raw XML by default
- no raw bytes
- no complete project object graph
- no complete animation frame/transform dump

`ProjectSearch` already caps display values at 500 characters; preserve that behavior.

## `InspectBehavior`

Input:

```text
path
findingLimit = 50
```

Compose:

```text
InputFilePolicy / HkxTextEdit.TextOf
BehaviourGraphModel.Parse
GraphValidator.Check
RoundTripReport.ForFile
HkReferences
```

Return compact typed data:

```text
path
file
readable
objectCount
rootObjectId
rootClass
classCounts[]
referenceCount
errors
warnings
roundTripLosses
findings[]
roundTrip[]
truncated
```

Do not return the full text model or all object fields.

Do not rely on `HkxBehaviorParser.LastObjects` as shared state. It is static mutable state. If the parser is needed internally, copy any required data into the result during the call.

## `ResolveProjectChain`

Input:

```text
path
animationLimit = 50
```

Compose `ProjectChain.Resolve`.

Return:

```text
root
links[]
animations[]
animationSources[]
boneCount
skeletonPath
problems[]
truncated
```

Links preserve:

```text
role
declared
resolved
exists
note
```

Do not claim archive/load-order resolution unless the operation actually received the same `GameData` context required to prove it.

Before treating declaration output as proven for model use, add the committed-character-fixture regression described in `assistant-character-declaration-contract.md`.

## `CheckProject`

Input:

```text
path
fileLimit = 50
findingLimitPerFile = 50
```

Compose:

```text
ProjectChain.Resolve
ProjectCheck.Run
```

Return totals plus bounded file results:

```text
root
filesFound
filesReturned
errors
warnings
unreadable
roundTripLosses
filesWithRoundTripLosses
files[]
truncated
```

Per file:

```text
path
name
error
errors
warnings
roundTripLosses
findings[]
roundTrip[]
truncated
```

Keep graph findings, unreadable-file errors, and round-trip losses distinct.

## `SearchProject`

Input:

```text
path
query
limit = 50
```

Compose:

```text
ProjectChain.Resolve
ProjectSearch.Run
```

Return:

```text
query
filesFound
filesRead
filesUnreadable
hits[]
problems[]
truncated
```

Preserve existing hit fields:

```text
path
file
kind
objectId
className
field
value
```

The model-facing façade passes its lower limit explicitly. Do not change `ProjectSearch.DefaultResultLimit` simply to optimize model context.

## `InspectAnimation`

Input:

```text
path
annotationLimit = 50
boneLimit = 50
```

Compose:

```text
HkxBinaryReader.TryReadAnimation / ReadAnimation
RoundTripReport.ForFile
```

Return:

```text
path
file
animationClass
supported
duration
numFrames
fps
numTracks
numBlocks
boneCount
bones[]
annotationCount
annotations[]
originalSkeletonName
blendHint
roundTrip[]
truncated
```

Annotation:

```text
time
text
```

Do not return tracks/translations/rotations/scales. A future explicit frame tool must require bounded track/frame ranges.

## `InspectObject`

Input:

```text
path
objectId
fieldLimit = 50
referenceLimit = 50
```

Compose:

```text
HkxTextEdit.TextOf
BehaviourGraphModel.Parse
BehaviourGraphModel.Get
HkReferences.In
ElementSummary.For
```

Return only the requested object plus bounded samples:

```text
path
file
objectId
className
scalars[]
lists[]
structs[]
structLists[]
outgoingReferences[]
incomingReferences[]
elementSummaries[]
truncated
```

Suggested field shape:

```text
name
kind
count
value
sample[]
```

Incoming references are `HkReferences.In(model)` sites whose target equals the requested object ID.

Never recursively serialize the reachable graph.

## Path authorization boundary

The MCP host authorizes paths before invoking this service.

Authorization must return the canonical approved path that is subsequently opened. Do not authorize one spelling and later reopen a different untrusted spelling.

The host owns:

```text
allowed roots
canonical component-based containment
static symlink/junction/reparse refusal
path_not_allowed / path_link_not_allowed
```

Existing core file policies remain authoritative for size and consistent reads after authorization.

Do not push model-specific root policy down into `ProjectSearch`, `ProjectChain`, or individual parsers.

## Cancellation/concurrency

Expose `CancellationToken` at long-running façade boundaries.

If an existing synchronous domain call cannot observe cancellation internally in the first slice, cancellation may suppress result publication only after that call completes. Do not use unsafe thread interruption.

Do not introduce global current-project/current-document state into core inspection.

## Error translation examples

```text
outside root -> error / path_not_allowed
static link component -> error / path_link_not_allowed
missing file -> error / path_not_found
unsupported describable HKX state -> partial or error / unsupported_hkx
project has missing animations -> successful inspection with typed project problems
one project file unreadable -> partial with typed file error
```

## Core proof before MCP wiring

Required focused tests:

```text
InspectBehavior returns bounded typed findings on committed behavior fixture
InspectBehavior reports round-trip state without XML
ResolveProjectChain preserves typed links/problems
committed character fixture proves bundled declaration extraction
CheckProject keeps graph and round-trip counts distinct
CheckProject returns partial data when one file is unreadable
SearchProject preserves typed hits and 500-character value cap
SearchProject defaults to 50
SearchProject clamps request >200 and never returns >200
limit <=0 -> invalid_argument
clamped request with <200 real items does not falsely mark truncated
InspectAnimation returns summary/annotations without frame arrays
InspectAnimation distinguishes unsupported animation subtype
InspectObject returns requested object only with bounded samples
InspectObject reports incoming/outgoing references
missing object -> object_not_found
missing file -> path_not_found
malformed input does not leak stack trace
```

Use committed fixtures, never a required local Fallout 4 install.

## Adapter responsibilities

MCP adapter contains only:

```text
stdio startup
tool registration/metadata
argument binding
root authorization
AssistantInspection calls
serialization
stderr logging
```

Desktop assistant uses the same service for disk-backed inspection and a separate editor-aware read adapter for unsaved active-document state.

## First source stop point

```text
AssistantInspection + typed models
focused core tests
path-authorizer tests
stdio MCP host
inspect_behavior
search_project
protocol/tool-discovery tests
full BGS CI-parity gates
```

Do not mix in assistant UI, provider packages, editor writes, automatic Save, animation-frame edits, or cross-document repair.