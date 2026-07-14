# Third-party notices

FO4RecordEditor is distributed under the **GNU General Public License v3.0** (see [LICENSE](LICENSE)).

It has to be. The editor links **Mutagen** (GPL-3.0) to read and write plugin binaries, and bundles
**niftool**, which embeds **nifly** (GPL-3.0). GPL-3.0 is a copyleft license: a program that links
GPL-3.0 code and is then distributed to anyone else must itself be distributed under GPL-3.0, with
its source made available to the people who receive it. That applies whether it is published
publicly or handed to one person privately.

So: when you send this package to someone, you also owe them the source. The simplest way to satisfy
that is to publish the repository and link it from here.

## Components

| Component | License | Used for |
|---|---|---|
| [Mutagen](https://github.com/Mutagen-Modding/Mutagen) | GPL-3.0 | Reading and writing `.esp`/`.esm`/`.esl` records. Linked. **Patched** — see below. |
| [nifly](https://github.com/ousnius/nifly) (inside `niftool`) | GPL-3.0 | NIF parsing and repair, behind the `nif_*` tools. |
| [texconv](https://github.com/microsoft/DirectXTex) (DirectXTex) | MIT | DDS decode for texture previews. |
| [WPF-UI](https://github.com/lepoco/wpfui) | MIT | The desktop UI theme and controls. |
| [Markdig.Wpf](https://github.com/Kryptos-FR/markdig.wpf) | MIT | Markdown rendering in the docs panel. |
| [Newtonsoft.Json](https://github.com/JamesNK/Newtonsoft.Json) | MIT | JSON, including the MCP wire format. |
| [YamlDotNet](https://github.com/aaubry/YamlDotNet) | MIT | Spriggit YAML interop. |
| [Roslyn scripting](https://github.com/dotnet/roslyn) (`Microsoft.CodeAnalysis.CSharp.Scripting`) | MIT | The `run_script` C# escape hatch. |
| [WebView2](https://developer.microsoft.com/microsoft-edge/webview2/) | Microsoft proprietary, redistributable | Hosts the editor's web UI. |

## Our Mutagen is patched

The bundled Mutagen is a fork with two fixes that upstream does not have. Without them the editor
writes broken perk data, so a clean upstream Mutagen is **not** a drop-in replacement:

- `Mutagen.Bethesda.Core/Translations/Binary/EnumBinaryTranslation.cs` — adds the missing case for
  short-backed (2-byte) enums, which previously threw `NotImplementedException` on write.
- `Mutagen.Bethesda.Fallout4/Records/Major Records/Perk.cs` — writes the EPF3 perk-choice flags as a
  raw 2-byte value, mirroring how the read side already parses them.

Both are committed to the Mutagen fork's own history, with the reasoning in the commit message.

## What is not here

This package contains **no Bethesda assets** — no game files, no vanilla scripts, no Creation Kit
binaries. `compile_papyrus` calls the Creation Kit's own `PapyrusCompiler.exe` from your local
install; it is not redistributable and is not included.

xEdit is not bundled either. The editor does not use it or need it.
