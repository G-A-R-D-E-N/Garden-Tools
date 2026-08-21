# Contributing — how this archive stays in sync

This repository is a documentation archive, not a codebase: the *sources of truth* live in
`ReverseEngineering/` (and the product's own repo, `G-A-R-D-E-N/BGS` on GitHub, which is not
part of this archive), and the root-level files are curated views over them. The rule of thumb for every edit here is:

> **Write the fact once, in the source document. Then update every root file that views that fact.**

Nothing in the root files should ever be the *only* place a fact lives.

---

## 1. The files and their roles

| File | Role | Source of truth |
|---|---|---|
| [`ReverseEngineering/03-FINDINGS.md`](./ReverseEngineering/03-FINDINGS.md) | The dated findings log — **the canonical record**. Every new fact lands here first. | the Ghidra pipeline |
| [`WORK-LOG.md`](./WORK-LOG.md) | The month-by-month narrative distilled from the sources. | 03-FINDINGS, CHANGELOG, `docs/findings/` |
| [`FINDINGS-INDEX.md`](./FINDINGS-INDEX.md) | One row per dated 03-FINDINGS entry — the lookup table. | 03-FINDINGS headings |
| [`ISSUES-MAPPING.md`](./ISSUES-MAPPING.md) | BGS issues/PRs ↔ findings, releases, records. | GitHub API + workspace records |
| [`MANIFEST.md`](./MANIFEST.md) | What is tracked here vs kept local, and why. | the `.gitignore` + the push policy |
| [`LEGAL-README.md`](./LEGAL-README.md) | Provenance & licensing of third-party-derived content, and the exclusion rationale. | the audit trail (§2 of MANIFEST) |
| [`README.md`](./README.md) | The hub: contents, narrative summary, pointers to everything. | all of the above |

## 2. Adding a new finding (the canonical path)

1. **Write it in `03-FINDINGS.md`.** Dated entry, newest first, with the entry's own convention:
   *what* (the fact), *how verified* (method + numbers), *where used* (consumer). There is a
   `## Template for new entries` at the bottom of the log — copy its shape.
2. **Add the row to `FINDINGS-INDEX.md`.** Newest first, one line: date | linked title | summary.
   The **link anchor must match the heading you just wrote** — see §4 for the slug rule and the
   one-command check.
3. **Touch `WORK-LOG.md` if the finding is month-narrative-worthy.** Add a bullet under the right
   date (the log is newest-month-first, and within a month it mirrors the log's order). Most
   findings deserve at least a line; don't restate the whole entry, point to it.
4. **Touch `ISSUES-MAPPING.md` if the finding relates to an issue/PR.** Add the cross-reference in
   the relevant table row (or the section-4 block for tracker-only items like #44/#55). A finding
   that *resolves* an issue should say so; a finding that just *informs* one says "related".
5. **Commit.** Author as **NomadsReach**, no AI author/co-author markers (workspace convention),
   message in the repo's imperative style (`docs: ...` for doc-only changes).

### Sync checklist — "I changed X, so I must update Y"

| If you … | then also update |
|---|---|
| add/edit an entry in `03-FINDINGS.md` | `FINDINGS-INDEX.md` (row + anchor), `WORK-LOG.md` (date line), `ISSUES-MAPPING.md` (if issue-linked) |
| change the *title* of an existing 03-FINDINGS heading | `FINDINGS-INDEX.md` row **and its anchor** (the slug changes) |
| add a finding that resolves/closes an issue or PR | `ISSUES-MAPPING.md` — move/annotate the row (status, "resolved by") |
| update the BGS changelog with a release | `WORK-LOG.md` release table + month narrative; `ISSUES-MAPPING.md` shipped-table rows if issue-linked |
| add/remove a **tracked file or folder** | `MANIFEST.md` (its whole job) and, if it's a work area, `README.md`'s area section |
| add a root-level reference file | `README.md` Contents table + ToC |

## 3. What must never be added

The standing rule: **no PDBs, no copyrighted or redistributable-only material.** Concretely, never
stage: `.pdb`/`.idb`/`.idc`, game binaries or data (`.exe`, `.dll`, `.nif`, `.hkx`, `.ba2`, `.bsa`,
`.esm`, …), decompiled code (the `Decompiles/` dumps, `DECOMPILE-RECORDS.md`, `Findings/Havok/*.txt`),
Havok SDK material (headers, SDK, Content Tools — even copies under a `reference/` folder), raw
symbol/label dumps, or third-party tooling (including checkouts of other people's repos, whatever
their license). The root `.gitignore` enforces most of this — but **a gitignore is not a policy**:
check the staged set before committing (§5), and if a file shouldn't be in the archive, leave it
out and document it in `MANIFEST.md` instead.

If you deliberately exclude something, `MANIFEST.md` section 2 is where that decision gets
recorded — an exclusion nobody can see is as bad as an accidental inclusion.

## 4. Anchors: the slug rule

This repo renders on **GitLab**, and the links into `03-FINDINGS.md` and within `README.md` use
GitLab's heading-anchor rule (documented in [the GLFM docs](https://docs.gitlab.com/user/markdown/#heading-anchors)):

1. lowercase the heading;
2. keep only letters, digits, `_`, `-` (this strips `—`, brackets, `/`, `:`, `,`, `.`, backticks,
   emoji — everything else);
3. turn **every space** into a hyphen — runs of hyphens are **not** collapsed, and a removed
   character sitting between two spaces leaves `--` (e.g. `2026-08-14 — title` →
   `2026-08-14--title`);
4. a heading whose slug was already used gets `-1`, `-2`, … appended, in document order.

Example — `## 2026-08-14 — [CONFIRMED] The OG Address Library is contiguous, so a wrong-version
lookup never fails` becomes
`2026-08-14--confirmed-the-og-address-library-is-contiguous-so-a-wrong-version-lookup-never-fails`
(the `--` after the date is the two spaces around the `—`).

**Check after any edit that touches a linked heading** (run from the repo root):

```bash
python3 - <<'EOF'
import re, unicodedata
def slug(h):
    out = []
    for ch in h.lower():
        cat = unicodedata.category(ch)
        if ch == " ": out.append("-")
        elif ch in "-_": out.append(ch)
        elif cat.startswith(("L", "N")): out.append(ch)
    return "".join(out)
def doc(path):
    seen, m = {}, {}
    for ln in open(path, encoding="utf-8"):
        g = re.match(r"^#{1,6}\s+(.*?)\s*#*\s*$", ln)
        if not g: continue
        s = slug(g.group(1))
        if s in seen: seen[s] += 1; s = f"{s}-{seen[s]}"
        else: seen[s] = 1
        m[s] = 1
    return m
src = doc("ReverseEngineering/03-FINDINGS.md")
used = set(re.findall(r"03-FINDINGS\.md#([a-z0-9_-]+)",
                      open("FINDINGS-INDEX.md", encoding="utf-8").read()))
print("broken anchors:", sorted(a for a in used if a not in src) or "none")
EOF
```

## 5. The pre-commit checks

Before every commit to this repo, the cheap checks that have caught real problems:

```bash
# 1. Banned material in the index? (expect nothing)
git diff --cached --name-only | grep -iE '\.(pdb|idb|idc|exe|dll|nif|hkx|hkp|ba2|bsa|esm|esp|sqlite|mcpb|blend|glb|jar)$' || true
# 2. Havok confidential markers in committed content? (expect 0)
git grep -il "telekinesys" HEAD | wc -l
# 3. Anchors valid (see §4)
# 4. Nothing huge slipped in (the largest tracked file today is ~10 MB — flag anything near it)
git ls-tree -r -l HEAD | sort -k4 -rn | head -3
```

These are the exact checks that caught the bundled Havok SDK headers and the third-party clones
during the original archive build — run them on every archive commit, not just the first one.

## 6. Editing under the repo's constraints

- The workspace root is a git repo whose worktree contains *other* repos (`Tools/*/`,
  `ReverseEngineering/re-mcp/` have their own `.git`; `BehaviourGraphStudio/` too, but it is
  gitignored — its records are local-only, see `AGENT-MAP.md`). Stage archive files by explicit
  path; `git add <dir>` of a nested repo either refuses (embedded-repository warning) or does the
  wrong thing.
- `core.filemode` is `false` here (the filesystem marks everything executable) — don't "fix" mode
  changes, they're noise.
- The nested repos are the user's own; nothing in them is ever rewritten from the archive side.
