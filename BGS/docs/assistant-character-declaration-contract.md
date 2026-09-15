# BGS character animation declaration contract

## Purpose

A behavior clip can reference a valid animation that still fails to load correctly when the character definition does not declare that asset. This document pins the actual Fallout 4 Havok shape BGS has to support before an assistant receives any character-declaration write authority.

This is a cross-document operation. The character HKX is not necessarily the active behavior document.

## Source trace

The shipped Fallout 4 class metadata defines `hkbCharacterStringData.animationBundleNameData` as:

```text
TYPE_ARRAY
  element: hkbAssetBundleStringData
```

`hkbAssetBundleStringData` is:

```text
bundleName  : TYPE_STRINGPTR
assetNames  : TYPE_ARRAY<TYPE_STRINGPTR>
```

Therefore the declaration is not a top-level text array that can be safely edited with `NativeAuthoringPlan.SetTextArray`.

The shipped `hkbCharacterStringData` metadata in this build does not expose a direct `animationNames` member. `ProjectChain.DeclaredAnimations` still accepts `animationNames` as a compatibility/fallback read path, but the proven Fallout 4 bundle representation is `animationBundleNameData[].assetNames`.

## Existing read path

`ProjectChain.DeclaredAnimations` currently combines:

```text
characterStringData.Strings("animationNames")
characterStringData.Strings("animationBundleNameData")
```

At first glance the second call looks inconsistent with the native class shape because `animationBundleNameData` is an array of structs.

The current `BehaviourGraphModel.ParseBody` behavior explains it: while parsing a top-level array of anonymous struct objects, nested `hkcstring` values encountered at that array depth are appended to the top-level `Lists[current]`. For `animationBundleNameData`, the nested `assetNames` strings are therefore flattened into `Lists["animationBundleNameData"]`, which is the list `DeclaredAnimations` consumes.

Do not replace this read path merely because the native field is a struct array. A source-level trace shows why it works.

## Missing regression

The repository contains a committed pristine Fallout 4 character fixture:

```text
BGS/tools/tests/fixtures/vanilla/Meshes/Actors/RadRoach/Characters/RadRoachCharacter.hkx
```

The save-fidelity suite proves that fixture can no-op round-trip according to its manifest contract, but there is no focused test proving that project/declaration parsing returns its bundled `assetNames`.

Before changing `ProjectChain.DeclaredAnimations` or the parser, add a focused regression using this fixture or another committed character fixture that proves:

```text
NativeXml / BehaviourGraphModel can observe hkbCharacterStringData
animationBundleNameData contains one or more bundle entries when present
the nested assetNames are returned by ProjectChain.DeclaredAnimations
returned names survive the same normalization ProjectChain.AnimationKey uses
```

If the vanilla fixture legitimately has zero asset names, use a second committed fixture with a known non-empty bundle instead of manufacturing production assumptions from a synthetic-only file.

## Write support is not present yet

`NativeAuthoringPlan.SetTextArray` only accepts an object field whose native type is directly:

```text
array of stringptr
array of cstring
```

That does not describe `hkbCharacterStringData.animationBundleNameData`.

`NativeAuthoringPlan.ResizeStructArray` can resize a top-level array of structs and `SetStructMember` can write supported members inside an element. However `NativeSave.StructElementWritable` currently accepts pointers, numeric/enums, wide numeric values and other in-place scalar-like members. A nested `TYPE_ARRAY` member is explicitly not an in-place supported struct member.

That means this is not currently a proven write:

```text
animationBundleNameData[i].assetNames
```

Do not route it through `SetStructMember` and do not generate a raw XML/binary patch.

## Required native operation

Before exposing `add_character_animation_declaration`, BGS needs a specific native primitive for a text-array member inside a struct-array element.

Conceptual core API:

```text
SetStructTextArray(
  objectId,
  field,
  element,
  member,
  values)
```

The public assistant operation should remain semantic:

```text
add_character_animation_declaration
remove_character_animation_declaration
```

The model should never receive the generic native primitive directly.

## Native writer proof requirements

The new nested-text-array primitive must prove all of the following before it is used by the assistant:

1. The owning object is `hkbCharacterStringData`.
2. The top-level field is `animationBundleNameData` or another specifically allowed declaration field proven by a fixture.
3. The struct element class is `hkbAssetBundleStringData`.
4. The member is `assetNames` and is `TYPE_ARRAY<TYPE_STRINGPTR>` in shipped metadata.
5. Existing bundle elements and `bundleName` values remain unchanged unless explicitly targeted.
6. Existing `assetNames` values preserve order and exact spelling except for the approved append/remove.
7. Duplicate additions are idempotent or explicitly refused.
8. Array local-fixup entries are rebuilt correctly when the string pointer array moves.
9. `SaveVerifier` is extended or already sufficient to verify the nested array content.
10. Reopening through `NativeXml`/`BehaviourGraphModel` and `ProjectChain.DeclaredAnimations` returns the edited declaration.
11. The character fixture still satisfies the appropriate fidelity contract for all unrelated content.

## Bundle selection

The assistant must not guess which bundle element should receive a new animation.

The preview must identify:

```text
character file
hkbCharacterStringData object
bundle index
bundleName
current asset count
new animation name
whether the name already exists in another bundle
```

If the character has more than one plausible bundle and BGS cannot derive the correct one from existing data, the operation must stop for user selection.

Do not silently create a new bundle unless a separate `create_animation_bundle` workflow is proven.

## Cross-document ownership

The first clip edit is intentionally an active-document editor operation. Character declarations are different because the character file may not be open.

Initial safe workflow:

```text
1. diagnose behavior + character relationship
2. preview clip edit separately
3. preview character declaration edit separately
4. user approves each target independently
5. each operation captures its own source stamp/revision
6. validate the combined project state in memory where possible
7. keep active editor edits unsaved
8. do not publish the character-file change automatically until a dedicated transaction path is proven
9. save explicitly
10. reopen both files and rerun ProjectChain/GraphValidator checks
```

An external MCP process remains read-only and must not acquire this write operation.

## Approval identity

A declaration approval must bind to at least:

```text
character canonical path
source stamp/hash identity
hkbCharacterStringData object id
bundle index
bundleName
old assetNames digest/count
requested animation name
operation kind: add/remove
```

If any of these change before application, the approval is stale and the edit must be rebuilt/re-approved.

## Validation preview

Before approval, report:

```text
animation name
whether the asset resolves loose/archive through current GameData
whether the name is already declared
which behavior clips reference it
which bundle will change
old/new asset counts
ProjectChain problems before/after where computable
```

Adding a declaration does not prove the asset exists. A missing asset must remain a separate diagnostic.

Removing a declaration requires searching consumers first and must warn on every clip reference BGS can identify.

## Tests required before assistant exposure

### Read regression

```text
committed vanilla character fixture exposes bundled assetNames through DeclaredAnimations
bundle-name text is not mistaken for an animation asset
multiple bundle elements flatten all assetNames without duplication introduced by the parser
```

### Native nested text-array writer

```text
append to existing assetNames
remove one asset while preserving neighbors/order
duplicate append is idempotent/refused by semantic layer
empty assetNames result is valid when the native format allows it
other bundle element remains byte/structurally unchanged
bundleName remains unchanged
source change before publication refuses
save verifier catches corrupted nested text-array output
```

### Project acceptance

```text
open pristine fixture
build declaration candidate
apply approved semantic operation
save through verified transaction
reopen character
DeclaredAnimations contains intended asset
ProjectChain resolves the same project/character/behavior/skeleton links
unrelated fixture content satisfies fidelity expectations
```

### Stale/approval

```text
source changes after preview -> refuse
bundle index/name changes after preview -> refuse
already-declared name at apply time -> rebuild result/no duplicate
```

## Phase ordering impact

Character declaration writes move one step later than the first clip mutation:

```text
Phase 1  read-only MCP + inspection façade
Phase 2  in-app assistant
Phase 3  active-document set_clip_animation
Phase 4a declaration read regression on real fixture
Phase 4b nested struct text-array native writer + verifier proof
Phase 4c approved character declaration semantic operation
Phase 5  broader multi-document repair coordination
```

This keeps the current diagnostic read behavior usable while refusing to pretend the native declaration write already exists.