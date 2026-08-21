# BGS issues & PRs ↔ findings, releases

How the GitHub issues and pull requests of **Behaviour Graph Studio**
([G-A-R-D-E-N/BGS](https://github.com/G-A-R-D-E-N/BGS)) relate to the findings in this workspace and
the shipped releases. Every row that has a documented relationship carries the pointer to it — the
findings entry, the engineering record, or the changelog — so each item can be traced in both
directions: *"which issue did this finding resolve?"* and *"what did the work behind this issue
teach us?"*

**Sources:** the GitHub API snapshot below (state, titles, merge timestamps as of 2026-08-17) plus
the workspace's own records — `ReverseEngineering/03-FINDINGS.md`,
`ReverseEngineering/converter-handoff.md`, and the local-only BGS-side records
(`BehaviourGraphStudio/docs/findings/`, `CHANGELOG.md`, `docs/superpowers/` — not tracked
here; see [`AGENT-MAP.md`](AGENT-MAP.md) for how to find them).
Two item numbers referenced in the workspace records (**#44**, **#55**) are **not** in the public
GitHub tracker (its numbering ends at #30) — they live on the private self-hosted tracker and are
mapped here from the workspace's own records, marked accordingly.

---

## 1. Shipped → release

All closed PRs merged **2026-08-15** — that is the day GitHub `main` was consolidated (see the local reconciliation record,
`BehaviourGraphStudio/docs/findings/reconcile-local-and-github-main.md` — local-only, pointed
to from [`AGENT-MAP.md`](AGENT-MAP.md)),
so the merge timestamp is a consolidation date, and the *content* of each PR is what maps to the
1.0.4 / 1.0.5 changelog entries.

| PR | Title | Merged | What it did / shipped in |
|---|---|---|---|
| #21 | Verify struct member writes byte-for-byte | 08-15 | The save-pipeline verification theme of the [2026-08-09 write-path audit](FINDINGS-INDEX.md) — struct member writes proven byte-for-byte before shipping (native save, **1.0.4**). |
| #17 | Compare negative narrow integers as signed when verifying a save | 08-15 | `SaveVerifier` signed-compare fix (narrow integer fields) — save-verification hardening, **1.0.4/1.0.5** safety work. |
| #4 | Scope save recovery to generated artifacts | 08-15 | Recovery only touches files BGS itself created during a save — **1.0.5** "Safer Saving". |
| #2 | Harden native save and archive handling | 08-15 | Native save + `.ba2` hardening (refusal-with-reason, `.bak` backups, unsafe-path rejection) — **1.0.4/1.0.5**. |
| #5 | Bridge home deck and GitLab pipeline work | 08-15 | The Bridge home tab + CI pipeline — **1.0.5**. |
| #6 | Release 1.0.5 | 08-15 | The 1.0.5 release itself. |
| #7 | Build the release on the Linux runner | 08-15 | Release CI on the Linux runner (the release pipeline BGS ships). |
| #8 | Install the pinned SDK where the runner can write | 08-15 | CI fix — SDK install path. |
| #14 | Set the SDK install dir on the step, not the job | 08-15 | CI fix — follow-up to #8. |
| #16 | Rewrite the changelog with collapsible sections | 08-15 | The collapsible CHANGELOG format used for 1.0.3–1.0.5. |
| #1 | Bump Avalonia and 6 others | *closed, not merged* | Closed without merge (superseded by the consolidation). |

## 2. Open PRs — the current work

| PR | Title | Status & findings |
|---|---|---|
| #29 | Convert packfiles between 8-byte and 4-byte pointer layouts | **Resolves #24.** The layout converter — one walker derives both layouts from `HavokClassTypes.json`; validated against the retail 32-bit toolchain (out of tree) with the value cross-check. Handoff record: [`ReverseEngineering/converter-handoff.md`](ReverseEngineering/converter-handoff.md) (the BGS-side engineering record was dropped before push — see the MANIFEST). Caught the `hkbLayer`/`hkbBlenderGeneratorChild` HK_ALIGN16 bug (157 corpus files, only 2 crashed). Not yet in a release. |
| #30 | fix(gamedata): drop the concatenation artifact from vanilla SAPT paths | Branch `fix/sapt-concatenated-paths`. Directly related to the [2026-08-17 AnimTextData finding](FINDINGS-INDEX.md) — the subgraph id is built by joining SAPT paths with `"|"`, so path cleanliness matters for id computation. |
| #15 | Native behavior authoring, Havok conversion, ragdoll, and cloth physics foundation | The native-authoring foundation (branch `feature/native-authoring-havok-physics`). The umbrella the authoring issues below hang off; its shipped slice is the **1.0.4** native save pipeline. |

## 3. Open issues — the authoring program

| Issue | Title | Where it sits / related work |
|---|---|---|
| #9 | Add native batch animation and behavior builder | The native **writer** work. Enabler findings: [2026-08-05 Havok packfile WRITER](FINDINGS-INDEX.md) + the 950-class member sweep. Discussion established the rule recorded in `converter-handoff.md`: *shipped code has zero external tool or runtime dependency* (the oracle stays out of tree). Shipped portion: 1.0.4 native save. |
| #10 | Add high-level behavior graph authoring API | Authoring layer above #9/#15. |
| #11 | Add native Havok 2018+ tagfile import support | Related to the 08-05 finding that FO4 ships *packfile* (magic `57E0E057`), and that `saveTagfile` exists in the binary but is not what the game writes. |
| #12 | Add semantic Havok version conversion layer | Adjacent to #24/#29 — the converter already handles the layout-rules byte (`bytesInPointer`/`littleEndian`/`reusePadding`/`emptyBaseClass`) and refuses unsupported layouts (big-endian). |
| #13 | Build Ragdoll Studio | Product idea around the Havok physics model work (`HavokPhysicsModel.cs`). |
| #18 | Add ragdoll and physics preview | Preview layer of #13/#15. |
| #19 | Add scene container and asset-level class coverage | Class-coverage gap work (the 950-class sweep's named gaps: `hkcdStaticMeshTreeBasePrimitiveDataRunBase<...>`, the 2 unparseable initializers). |
| #20 | Expose native authoring in the editor | The editor surface for #15 — what becomes "native authoring" in the app. |
| #22 | Read and emit embedded class metadata in packfiles | Class tables: `HavokClassTypes.json` + the 08-05 finding that `hkClass` objects are zeroed on disk and only readable via the dynamic initializers. |
| #23 | Add skeleton and rig authoring | Skeleton/rig editor work (skeleton views shipped in 1.0.1). |
| #25 | Exercise native authoring UI in the headless smoke harness | `tools/uismoke` (`Smoke.cs`) — headless UI smoke tests. |
| #26 | Add skin binding and weight tools | Skinning/weight editing. |
| #27 | Figure out and add body-part/VATS region editing | Body-part/VATS regions. |
| #28 | Build the full character/race authoring workspace | The end-state workspace tying #23/#26/#27 together. |

## 4. Workspace-recorded items not on the public tracker

| Ref | Documented meaning | Source |
|---|---|---|
| **#44** | Native authoring of `variableBounds` arrays — *"the open half of #44"*. The 2026-08-09 write-path audit finding #2 (growing an empty array loses the do-not-free flag — local record, `BehaviourGraphStudio/docs/findings/2026-08-09-write-path-and-class-table.md`, see [`AGENT-MAP.md`](AGENT-MAP.md)) **blocks its second half**; the proposed fix is to force bit 31 in the capacity word wherever the pointed-at run lives inside the file. | `docs/findings/2026-08-09-write-path-and-class-table.md` (local) |
| **#55** | Persist freeform graph layout positions per path (keyed settings + graph drag notifications, `app/Settings.cs` / `app/GraphView.cs`). | `docs/superpowers/plans/2026-08-11-persist-graph-layouts.md` |

## 5. The headline arc, in one line

**#24 "Honour packfile pointer layout rules" → the [2026-08-05 writer RE](FINDINGS-INDEX.md) (fixup tables, section headers, layout bytes) → PR #29 (converter, validated out of tree) → the 4-byte output caught a real format bug (HK_ALIGN16) → next release.** Everything else in sections 1–3 hangs off that same writer/class-table spine.

---

*Regenerating:* refresh statuses with `gh api repos/G-A-R-D-E-N/BGS/issues?state=all` (and the
pulls endpoint for merge timestamps), and update section 4 when the private tracker becomes
reachable. The findings links point at [`FINDINGS-INDEX.md`](./FINDINGS-INDEX.md) — when that
index gains a row, this file's links follow automatically.
