# 52 - Cross-binary audit: docs 23-51 vs the runtime PDB

Audit trigger: the record-model docs were built almost entirely from
CreationKit.exe decompilation + shipped-file voting. This pass adds the
third evidence leg - the private **Fallout4.exe 1.10.155 PDB**
(`ReverseEngineering/Symbols/`) - and checks every class-level claim.

## 1. Why the PDB can and can't help

The PDB names *runtime* functions at *runtime* addresses; CreationKit.exe
is a different binary, so PDB names cannot be attached to our FUN_ RVA
attributions. What it CAN do independently: confirm which form classes
exist, which have form-factories (i.e., are authorable), what their
constructor signatures imply structurally, and - via absent methods -
prove inherited loaders.

## 2. Results

| check | result |
|---|---|
| Form-class factories | **84/88** attributed classes have `Concrete(Object)FormFactory<class,...>` in the PDB |
| Factory template ID vs form enum | **33/33** sampled IDs equal the 4CC's ENUM_FORM_ID index (three-way: PDB <-> CommonLibF4 <-> our mapping) |
| KEYM shares MISC loader (doc 38) | **proven**: PDB contains `TESObjectMISC::Load` (1352 B) and **zero** TESKey::Load - TESKey inherits it |
| ExtraCombinedRefs structure (doc 39) | ctor signature `(initialRefCount, initialKeyCount)` + `Clear()` matches the derived ref-count + keyed-hash layout |
| DFOB loader (doc 44) | `BGSDefaultObject::Load` exists (485 B) |
| LVLI/LVLN class (doc 37) | `ConcreteObjectFormFactory<TESLevItem,...>` present |

Four classes lack factories - all explained:

* `ExtraCombinedRefs`, `PathingCell`: not forms; internal objects.
  Consistent with docs 39/51.
* `BGSLayer`, `BGSDefaultObject` factory strings absent but
  BGSDefaultObject::Load exists; BGSLayer has no runtime methods at all -
  its attribution rests on CK-side RTTI + vtable evidence only (flagged,
  tier C below).

## 3. Evidence tiers after audit

* **Tier A** (file-voted + PDB-corroborated): all B/C/D-tier loader
  attributions except those noted below; effect-pair atomicity; leveled
  grammar; SCOL pairing; LAND sizes; census counts.
* **Tier B** (two independent lines): S-tier loaders (CK assert-string
  source paths + vtable calibration); XCRI constant decode (calibrated
  against in-function asserts).
* **Tier C** (single-source, labeled in-docs): TERM SNAM field semantics
  (measured pattern, meaning inferred); NVNM triangle +0x10/+0x13 names;
  BGSLayer (CK-only evidence).
* **Open**: NVNM 21-vs-16-byte reader tension (doc 51 §5-6) - unchanged
  by this audit; runtime PDB has no navmesh Load contradiction to add.

## 4. Corrections required

None. No doc claim was contradicted by the PDB; three claims moved up
to stronger tiers (KEYM, ExtraCombinedRefs, DFOB).
