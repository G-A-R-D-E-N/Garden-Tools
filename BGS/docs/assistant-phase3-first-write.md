# BGS assistant Phase 3 first controlled write

## Scope

The first model-requested mutation is deliberately narrow:

```text
bgs.set_clip_animation
```

It changes only `animationName` on one existing `hkbClipGenerator` in the currently active BGS editor document.

This operation is the in-app active-document mutation. External MCP remains read-only and does not
access the desktop document revision, undo stack, dirty state, selection, or unsaved model.

Do not expose a generic `set_field` tool.

Source implementation still requires Ponytail proof-first/reuse-first work.

## Why this operation

The existing path already has strong boundaries:

- manual inspector edits use `HkxTextEdit.SetParamAt`
- successful edits enter the editor `Commit` path
- `Commit` advances document revision and normal undo/dirty state
- `GraphValidator` treats blank clip `animationName` as a save blocker
- project-aware validation checks asset existence and character declaration membership
- save remains separate behind `GraphValidator.SaveRefusal` and `DocumentSaveTransaction`

This makes a clip reference a useful first mutation without granting arbitrary graph-write authority.

## Model input

```json
{
  "objectId": "123",
  "animationName": "Animations\\Movement\\Sprint.hkx"
}
```

No path argument.

The active desktop document is always the target.

## Two-stage operation

A model request does not mutate immediately.

```text
request
  -> build candidate preview
  -> render approval request
  -> user approves/rejects
  -> recheck captured state
  -> apply through editor session
  -> remain unsaved
```

Framework approval is only the conversation/UI signal. BGS owns candidate construction, stale checks, mutation, validation, undo semantics, and save authority.

## Preview

Preview is side-effect free.

Capture:

```text
document identity
documentStamp
objectId
objectClass
clipName
oldAnimationName
newAnimationName
wasDirty
undoDepth
projectRoot where known
```

Refuse when:

- no active text-editable behavior document exists
- document is read-only
- object does not exist
- object is not `hkbClipGenerator`
- new value is blank/whitespace
- new value equals current value

Build only an in-memory candidate:

```text
HkxTextEdit.SetParamAt(currentXml, objectId, "animationName", newAnimationName)
```

Then compare existing BGS validation before/after. Report introduced/resolved findings rather than returning both complete finding sets.

### Target resolution

When project context exists, report only what BGS can prove:

```text
existsUnderProject: yes/no/unknown
declaredByCharacter: yes/no/unknown
resolutionContext: project-root-only | game-data-aware | unknown-no-project
```

Do not claim archive-wide absence without the same `GameData` context used by project validation.

### Approval card

Minimum user-visible facts:

```text
Clip: #123 Sprint
Before: Animations\Movement\Old.hkx
After:  Animations\Movement\Sprint.hkx
Target exists: yes/no/unknown
Declared by character: yes/no/unknown
New blocking errors: N
New warnings: N
Resolved findings: N
Document already dirty: yes/no
```

Use factual language such as `BGS validation found no new blocking error`, not an unsupported claim that a candidate is globally safe.

## Approval identity

Approval binds to one exact candidate:

```text
document identity
documentStamp
objectId
oldAnimationName
newAnimationName
```

Rejection changes nothing.

Do not reuse approval for a regenerated proposal.

## Apply

Before applying, re-read current editor state.

Reject as stale if any changed:

- active document identity
- document stamp
- object class
- current `animationName`

A stale proposal requires a new preview and approval.

Apply through the same editor-session semantics as the production manual edit:

```text
set parameter
  -> HkxTextEdit.SetParamAt
  -> Commit
  -> normal editor refresh/dirty/undo behavior
```

Do not:

- assign `_xmlText` directly
- bypass `Commit`
- create an assistant-specific undo stack
- write the HKX file directly
- automatically invoke Save

Result should state:

```text
applied = true
saved = false
new documentStamp
isDirty = true
undoStepsAdded = 1
objectId
oldAnimationName
newAnimationName
validationDelta
```

The visible response must say the editor changed but the HKX is not saved yet.

## Save remains manual

Phase 3 adds no model save tool.

```text
approved editor mutation
  -> dirty document
  -> user may inspect/undo/check graph
  -> user chooses existing Save
  -> verified save transaction
```

A future save operation requires a separate approval/source-stamp/reopen contract.

## Validation delta

Use stable finding identity from the existing typed fields:

```text
level
objectId
where
what
blocksSave
```

Return bounded:

```text
introduced[]
resolved[]
remainingBlocking[]
```

Important clip-related cases include:

- blank-animation blocker
- missing animation
- animation not declared by character
- unrelated blockers unexpectedly introduced by the candidate

Never hide unrelated new findings.

## Proof requirements

### Side-effect-free preview

Preview leaves unchanged:

```text
current editor XML
documentStamp
dirty state
undo/redo depth
selection
disk bytes
```

### Refusals

Prove:

```text
missing object -> refuse
non-clip object -> refuse
blank value -> refuse
same value -> refuse
read-only document -> refuse
```

### Validation

Prove preview distinguishes:

```text
target missing
target present
target undeclared by character
valid existing/declared target with no new blocker
```

Archive-aware claims require matching `GameData` context.

### Approval/stale state

Using deterministic assistant function tests, prove:

```text
mutation request becomes approval request
rejection changes nothing
approval applies only exact object/value
document revision change rejects approval
old field value change rejects approval
document switch rejects approval
```

### Editor parity

After successful apply:

```text
documentStamp increments exactly once
dirty == true
undo depth increments exactly once
editor XML has new value
disk bytes unchanged
```

Undo must restore the exact prior editor XML/value without disk change.

Compare assistant application with the production inspector/session edit from identical starting data. The resulting editor XML, dirty state, finding state, and undo-depth delta should match.

A test-only direct setter is not proof of parity.

### Save/reopen acceptance

After approved mutation, invoke the normal existing Save path and prove:

1. `DocumentSaveTransaction` succeeds
2. backup behavior remains correct
3. saved HKX reopens
4. clip has the approved `animationName`
5. expected validation state remains after reopen

## Fixture strategy

Start with committed `SingleAnimFurniture.hkx` because it is small and fidelity-covered.

For project-aware tests, create only the surrounding temporary project arrangement needed to connect committed fixture contents. Do not invent new Havok payload semantics.

Use a larger behavior such as `WeaponBehavior.hkx` only after the narrow path is proven.

## Source extraction boundary

Create the smallest reusable editor-session operation needed for this mutation rather than rearchitecting `MainWindow`.

Conceptual internal API:

```text
PreviewClipAnimationChange(objectId, animationName)
ApplyClipAnimationChange(previewToken)
```

The token is an internal typed record containing the captured document identity/revision and old/new values. It is not a model-generated opaque capability string.

Provider/model types stay out of the editor-session layer.

## Next authority

Do not jump from this tool to generic field editing.

Preferred progression:

1. `set_clip_animation`
2. narrow symbols/bindings/graph operations with existing typed editor proof
3. typed graph authoring through proven BGS sessions
4. character declaration work only after `animationBundleNameData[i].assetNames` native write support is proven per `assistant-character-declaration-contract.md`
5. animation content edits after the UI-owned animation session is extracted/proven
6. explicit model Save last

## Acceptance

Complete only when all are true:

1. `set_clip_animation` is the only model mutation in this slice.
2. Preview has no side effects.
3. Exact preview requires explicit approval.
4. Stale approval cannot apply.
5. Apply reuses production editor commit semantics.
6. Exactly one undo step is added.
7. Document remains unsaved/dirty.
8. Undo restores exact prior editor state.
9. Validation delta is visible before approval.
10. No automatic Save occurs.
11. Manual Save uses the existing verified transaction and passes reopen proof.
12. External MCP has no active-editor or disk mutation operation.
13. Windows/Linux CI, xUnit, and Avalonia headless smoke remain green.
