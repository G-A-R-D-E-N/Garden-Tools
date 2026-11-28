# Third-party notices

FO4RecordEditor is distributed under the **GNU General Public License v3.0** (see [LICENSE](LICENSE)).

Radicle's NIF reader carries code copied from **G-A-R-D-E-N/commonlibf4** (GPL-3.0-or-later, see
below). GPL-3.0 is a copyleft license: a program that includes GPL-3.0 code and is then distributed
to anyone else must itself be distributed under GPL-3.0, with its source made available to the
people who receive it. That applies whether it is published publicly or handed to one person
privately.

So: when you send this package to someone, you also owe them the source. Publishing this repository
is how that obligation is satisfied; there is no separate location to also track.

Builds published before the vendored library was removed linked a patched copy of **Mutagen**
(GPL-3.0). Its source, including the patches, is in this repository's history before the commit that
deleted `PluginEditTool/Mutagen`; the current tree no longer contains or links it.

## Components

| Component | License | Used for |
|---|---|---|
| [G-A-R-D-E-N/commonlibf4](https://github.com/G-A-R-D-E-N/commonlibf4) | GPL-3.0-or-later, with the Fallout 4 modding exceptions | Shader-property flags and material ids, `NiAlphaProperty` accessors, BSX flags and the BSGraphics vertex-descriptor bit helpers, copied into Radicle's NIF reader at commit `22b0dbd` and adapted to its namespaces and style. |
| [bcdec.h](https://github.com/iOrange/bcdec) | MIT / Unlicense (dual) | BC1-BC7 block decoding, ported to C# (the managed copy was removed; the editor decodes through Radicle). |
| [WPF-UI](https://github.com/lepoco/wpfui) | MIT | The desktop UI theme and controls. |
| [Markdig.Wpf](https://github.com/Kryptos-FR/markdig.wpf) | MIT | Markdown rendering in the docs panel. |
| [Newtonsoft.Json](https://github.com/JamesNK/Newtonsoft.Json) | MIT | JSON, including the MCP wire format. |
| [Roslyn scripting](https://github.com/dotnet/roslyn) (`Microsoft.CodeAnalysis.CSharp.Scripting`) | MIT | The in-app C# script engine (`FO4RecordEditor/Services/ScriptEngine.cs`). |
| [WebView2](https://developer.microsoft.com/microsoft-edge/webview2/) | Microsoft proprietary, redistributable | Hosts the editor's web UI. |
| [ffmpeg](https://ffmpeg.org/) ([BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds) win64-lgpl static build) | LGPL-3.0 | Decodes/transcodes audio (and video's audio track) for the `audio_*` tools. Run as a separate process, not linked -- see below. Shipped as `tools\audio\ffmpeg.exe` when the build fetches its pin. |
| xWMAEncode.exe (Microsoft, classic DirectX SDK, 2009) | Microsoft, unmodified redistributable | Encodes/decodes Fallout 4's xWMA (`.xwm`) format for the `audio_*` tools. |
| BmlFuzEncode.exe / BmlFuzDecode.exe (BowmoreLover) | Freely redistributed alongside Yakitori Audio Converter | Packs/splits the `.fuz` voice container (xwm + lip) for the `audio_*` tools. |

### bcdec is a port, not a copy

The removed managed `BcnDecoder` was a C# port of bcdec.h v0.97 by Sergii Kudlai. It is the same
decoder Godot ships as `modules/bcdec`. Two things are carried across verbatim because they are
fixed spec data and cannot be derived: the 64 two-subset and 64 three-subset BC7 partition tables,
and the per-mode endpoint bit widths. BC6H (HDR) was not ported.

The port was checked against DirectXTex's own conversion of the same files, pixel for pixel, over
484 real mod textures covering every format: BC2, BC3, BC5, BC7 and the uncompressed layouts match
exactly, and BC1 and BC4 differ by at most 1 in the rounding of a rare interpolated value.

## Offline Creation Kit Wiki mirror

`tools\ckwiki\fallout4\` bundles a static HTML mirror of the community-run Creation Kit Wiki
(creationkit.com/fallout4), used by `papyrus_function_lookup`/`papyrus_script_info` so those tools
work without a network call. It is **not** Bethesda content -- it's wiki text written by the FO4
modding community, originally packaged for reuse as a Nexus "Modder's Resource":

- **Title:** Offline Fallout 4 Creation Kit Wiki
- **Content by:** FO4 CK Wiki Editors
- **Mirror packaged by:** tondabayashi (Nexus Mods)
- **Version bundled:** 21.05.20 (matches this package's `tools\ckwiki\fallout4\` exactly)
- **Tags on the source page:** Modder's Resource, Non-Playable Resource, Utilities for Modders

## Audio tools (`tools\audio\`)

Three third-party executables back the `audio_*` tools, all run as **separate processes** the editor
shells out to (like texconv) -- none of them are linked into the editor, so none of the
"combined work" complexity of linking GPL code applies here.

- **ffmpeg** -- `tools\audio\ffmpeg.exe` is a static Windows build from
  [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds) (the `win64-lgpl` variant, which
  excludes GPL-only components like x264/x265 -- everything in it is LGPL-3.0 or more permissive).
  LGPL-3.0's source-availability obligation is satisfied by ffmpeg's own upstream source
  (ffmpeg.org) and BtbN's public, reproducible build scripts; nothing about this project modifies
  ffmpeg itself. Replaced the workspace's previous bundled copy, a 2012 nightly (`N-36890`) with
  13 years of unpatched codec parsers -- see the Code Notes entry for that decision. When the build fetches
  its pin from `PluginEditTool/tools/bundled-tools.json`, the zip gets this exact build, checked by
  SHA256 before packaging. The source it was built from is FFmpeg commit
  `7adfbe40dcde598f93a6963d2f7b22cd75202ccc`, built with `--enable-version3`; the license text is
  `COPYING.LGPLv3` at that commit, shipped as `tools\audio\LICENSE-ffmpeg.txt`. An `ffmpeg.exe`
  supplied through `FO4IDE_AUDIO_TOOLS_DIR` is not this build and is not covered here. The library
  list compiled into this build is in BtbN's build scripts at the pinned tag (`variants/win64-lgpl.sh`,
  `variants/defaults-lgpl.sh`, `scripts.d/`).
- **xWMAEncode.exe** -- Microsoft's own tool from the classic DirectX SDK (June 2010 era; this build
  reports itself as "build 9.29.1962.0", copyright 2009). Not open-source, but it is the *only* tool
  that reads/writes Fallout 4's proprietary xWMA format, and it has been freely redistributed by
  essentially every FO4/Skyrim audio-conversion modding tool for over a decade (JohnB's Skyrim Audio
  Converter, BowmoreLover's Yakitori Audio Converter, Backporter's SSE/FO4 Sound-Music Converter,
  and the raw batch-file tools in this workspace's `Tools\Audio Converter\` all bundle the identical
  binary) -- there is no indication Microsoft has ever objected to this, and no alternative exists.
- **BmlFuzEncode.exe / BmlFuzDecode.exe** -- small command-line tools by BowmoreLover, distributed
  alongside Yakitori Audio Converter on Nexus Mods, that pack/split the `.fuz` voice container
  (an xwm audio stream + a `.lip` lip-sync file). Freely redistributed the same way as the tools
  above; no separate license file ships with them.

## What is not here

This package contains **no Bethesda assets** -- no game files, no vanilla scripts, no Creation Kit
binaries. `compile_papyrus` calls the Creation Kit's own `PapyrusCompiler.exe` from your local
install; it is not redistributable and is not included.

xEdit is not bundled either. The editor does not use it or need it.

`Archive2.exe` and `PapyrusCompiler.exe` are Bethesda's and come with the Creation Kit. The editor
detects them in your Fallout 4 install and does not ship them.
