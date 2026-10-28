# Releases

## Current situation

GARDEN-TOOLS had no release process: its only tags are `bgs-v1.0.5`, `bgs-v1.1.0`, `bgs-v1.2.0`
and `bgs-v1.3.0`. `main` is untouched and PR #848 (`nomad/dev` -> `main`) stays unmerged, so a
FO4IDE/Radicle release is built from, and tagged on, a `nomad/dev` commit.

Nothing publishes automatically: `.github/workflows/release.yml` is a build-only lane with
`permissions: contents: read`, so it cannot create tags or releases even by accident.

## Build lane

1. Forgejo Actions -> `release` -> **Run workflow**, with `version` (e.g. `0.1.0-alpha.1`) and
   `ref` (commit, tag or branch; default `nomad/dev`).
2. `fo4ide-package` (Windows): checks out the ref, runs `setup-msvc-ccache.ps1` and
   `pinned-zlib-windows.ps1`, does `npm ci` + `npm run build` in `PluginEditTool/web`, then
   `PluginEditTool/package.ps1 -Version <version> -SkipWeb` with `FO4IDE_CMAKE_GENERATOR=Ninja`.
   The artifact `FO4RecordEditor-<version>-windows-x64` is a Forgejo artifact zip that contains
   the release file `FO4RecordEditor-<version>.zip`; `release-metadata` holds version, ref,
   resolved commit, `RADICLE_ABI_VERSION`, runner name and UTC build time.
3. `radicle-source` (Linux): `git archive` of `Radicle/` as `radicle-<version>-source.zip`
   (inside the `radicle-<version>-source` artifact zip), plus `radicle-source-info`.
4. Artifacts are retained 30 days; download and unzip them from the run before then.

`PluginEditTool/package.ps1` is the authority for what the zip contains: `FO4RecordEditor.exe`,
`web/dist`, `tools/*` when the optional tool paths resolve, `README.md`, `LICENSE`,
`THIRD_PARTY_NOTICES.md`, `docs/MCP_SETUP.md` and `mcp.sample.json`.

## Publish (owner-approved, after reviewing the artifacts)

A human does this with an owner-approved token; no credentials live in this repository.

1. Tag the built commit (the `commit=` line in `release-metadata.txt`) and push the tag:
   `git tag fo4ide-v<version> <commit>`, `git push origin fo4ide-v<version>`.
   Add `radicle-v<version>` at the same commit if a Radicle release is wanted.
2. Create the release on git.nomadicinteractive.dev with the zip attached:

```text
POST /api/v1/repos/GARDEN/GARDEN-TOOLS/releases
  {"tag_name": "fo4ide-v<version>", "target_commitish": "<commit>",
   "name": "FO4IDE <version>", "body": "<release notes>"}

POST /api/v1/repos/GARDEN/GARDEN-TOOLS/releases/{id}/assets?name=FO4RecordEditor-<version>.zip
  multipart body: the file
```

## Packaging reality

FO4IDE statically links Radicle (`PluginEditTool/native` builds it as a subproject), so the
FO4IDE zip already contains Radicle. Shipping a separate Radicle binary zip is the parked
PR #838 decision (owner-owned). The `Radicle/` source snapshot is not independently buildable:
`Radicle/CMakeLists.txt` checks the committed hkx layout table against
`../BGS/src/Hkx/*.json`, outside the
exported subtree, so the supported consumer path stays pinning the commit or tag in a full
clone.

Recommended default: publish the FO4IDE zip, tag Radicle at the same commit, and ship no
separate Radicle DLL unless the owner reopens #838.

## Open decisions for the owner

- CI build vs local build: the lane builds on the self-hosted runners. A local build of
  `PluginEditTool/package.ps1` on a Windows workstation with the same optional tools produces
  the same zip; the CI lane is the reproducible one and does not depend on one machine's tools.
- Tag source: a `nomad/dev` commit is the only option that does not need `main`.
- Tag naming: `fo4ide-v*` / `radicle-v*` (precedent: `bgs-v*`).
- Optional external tools (niftool, texconv, CK wiki mirror, audio tools): bundle them by
  setting the `FO4IDE_NIFTOOL_PATH`, `FO4IDE_TEXCONV_PATH`, `FO4IDE_CK_WIKI_PATH` and
  `FO4IDE_AUDIO_TOOLS_DIR` repository variables; unset means the zip ships without them.
