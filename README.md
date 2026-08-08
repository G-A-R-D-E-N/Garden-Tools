# FO4RecordEditor

A Fallout 4 plugin IDE. It is two things in one executable:

- **A desktop editor** -- browse, diff, and edit records across your whole load order, scan for
  conflicts, compile and decompile Papyrus, and inspect NIFs. Think xEdit, but with a modern UI.
- **An MCP server** -- the same 105 operations, exposed to an AI assistant (Claude Code, or any other
  MCP client). This is the part most people want: you describe the edit, the AI makes it, and the
  plugin is written by [Mutagen](https://github.com/Mutagen-Modding/Mutagen) rather than by hand.

It is built on a patched Mutagen, so it reads and writes real `.esp`/`.esm`/`.esl` binaries. It does
not go through xEdit and does not need xEdit installed.

It runs natively on **Windows and Linux** from one codebase. See
[Linux](#linux) below, or [RUNNING_ON_LINUX.md](../RUNNING_ON_LINUX.md) for the detail.

---

## Install (Windows)

**1. Install the .NET 9 Desktop Runtime (x64).** This is the only prerequisite.
   Get it here: <https://dotnet.microsoft.com/download/dotnet/9.0> -- pick **".NET Desktop Runtime 9.x", x64**.

> The app is deliberately *not* a self-contained single-file exe. Mod Organizer 2's virtual filesystem
> (usvfs) hooks file access, and a self-contained .NET host cannot load its bundled runtime through
> those hooks -- the process dies before any managed code runs, so the app "launches" and instantly
> closes with no log and no error. A framework-dependent build loads the runtime from the shared
> install instead, which usvfs handles fine. This is why the runtime is a separate install.

**2. Unzip this package anywhere.** Keep the folder together -- the exe loads `web\dist\` and `tools\`
   from beside itself.

**3. Run `FO4RecordEditor.exe`.** That is the GUI. If you only want the AI/MCP integration, you never
   need to open it -- but it is the fastest way to confirm the install works.

### Running it under Mod Organizer 2

Add `FO4RecordEditor.exe` as an executable in MO2 (Settings → Executables → `+` → Add from file).
Launching it *through* MO2 is what lets it see your modlist's virtual Data folder, which is what you
want when you are editing a plugin that overrides mods.

---

## Linux

```bash
sudo apt install ./packaging/linux/out/fo4recordeditor_1.0.0_amd64.deb
fo4recordeditor
```

The .deb is self-contained, so there is no runtime to install first. Build it with
`packaging/linux/build-deb.sh`.

It opens in its own WebKitGTK window, not a browser tab, the same way the Windows build hosts the UI
in WebView2. Same app, same code: the interop layer, the record editing, and the MCP server
(`fo4recordeditor --mcp --mo2 <instance>`) are all the identical compiled sources. Pass `--browser`
to use a browser window instead, or `--headless` to serve without opening anything.

A few helper tools (niftool, texconv, Archive2, PapyrusCompiler, xWMAEncode) only exist as Windows
binaries; installing `wine` makes the panels that drive them work. Everything else is native --
including Papyrus compiling, which has its own engine and does not need `PapyrusCompiler.exe`.

---

## Point your AI at it (MCP)

The editor speaks [MCP](https://modelcontextprotocol.io) over stdio. Full detail, including every
tool and the workflow rules that keep the AI from corrupting plugins, is in
**[docs/MCP_SETUP.md](docs/MCP_SETUP.md)** -- read that one. The short version:

Drop a `.mcp.json` in your project folder (a sample sits next to the exe as `mcp.sample.json`):

```jsonc
{
  "mcpServers": {
    "fo4editor": {
      "type": "stdio",
      "command": "C:\\Tools\\FO4RecordEditor\\FO4RecordEditor.exe",
      "args": ["--mcp", "--mo2", "C:\\Modlists\\My Modlist"]
    }
  }
}
```

- `--mcp` runs it headless as a server. The GUI never opens.
- `--mo2 <instance>` points it at an MO2 instance (the folder holding `mods\` and `profiles\`). It
  reads the active profile's `plugins.txt` and reconstructs that exact load order -- so the AI sees
  what the game sees, conflicts and all.
- `--data <folder>` is the alternative: a plain `Data` folder, for a vanilla (non-MO2) setup.

Use one or the other. Then restart your AI client, or run `/mcp reconnect` in Claude Code.

---

## Configuration

Settings live in `%APPDATA%\FO4RecordEditor\settings.json`, written by the GUI's settings panel. Every
path below is optional and auto-detected when blank -- set one only to override the detection.

| Key | What it points at | Detected from |
|---|---|---|
| `fallout4Path` | Fallout 4 install root | Registry, then the usual Steam library paths |
| `papyrusCompilerPath` | The Creation Kit's `PapyrusCompiler.exe` | `<Fallout 4>\Papyrus Compiler\` |
| `papyrusBaseImports` | Base script roots, `;`-separated | `<Fallout 4>\Data\Scripts\Source\Base` |
| `niftoolPath` | `niftool.exe` | The bundled copy in `tools\niftool\` |
| `texconvPath` | `Texconvx64.exe`; only a fallback now, textures decode in process | The bundled copy in `tools\texconv\` |
| `ckWikiPath` | Offline Creation Kit Wiki HTML mirror | The bundled copy in `tools\ckwiki\fallout4\` |
| `ffmpegPath` | `ffmpeg.exe` | The bundled copy in `tools\audio\` |
| `xwmaEncodePath` | `xWMAEncode.exe` | The bundled copy in `tools\audio\` |
| `archive2Path` | The Creation Kit's `Archive2.exe`; only used by `archive_pack` with `use_archive2` | `<Fallout 4>\Tools\Archive2\` |
| `mo2InstancePath` | MO2 instance | The `--mo2` argument |
| `dataFolder` | A plain `Data` folder | The `--data` argument |

Each also honours an environment variable, which wins over the settings file:
`NIFTOOL_PATH`, `TEXCONV_PATH`, `PAPYRUS_COMPILER_PATH`, `PAPYRUS_BASE_IMPORTS`, `CK_WIKI_PATH`,
`FFMPEG_PATH`, `XWMAENCODE_PATH`, `ARCHIVE2_PATH`. `BmlFuzEncode.exe`/`BmlFuzDecode.exe` (the .fuz
packer/splitter) are bundled-only, in the same `tools\audio\` folder, with no override -- there's
nothing to point them at.

### Papyrus compiling no longer needs the Creation Kit; BA2 packing never did

`compile_papyrus` has two engines, chosen with `engine`:

- **`builtin`** is this tool's own compiler -- lexer, parser, script index, resolver, type checker,
  code generator and `.pex` writer -- and needs **no `PapyrusCompiler.exe`**. It is measured against
  the Creation Kit's own output: of the 161 real scripts on the development machine it compiled,
  158 came out instruction for instruction identical to the `.pex` the CK produced from the same
  source, Bethesda's own 55 KB `ObjectReference.psc` among them.
- **`creationkit`** shells out to Bethesda's `PapyrusCompiler.exe`, which ships with the **Creation
  Kit**, is not redistributable, and is therefore not in this package. Install the CK (it is free, on
  Steam) and it is found automatically.
- **`auto`**, the default, uses the Creation Kit when one is installed and the built-in engine when
  there is not.

**One honest caveat.** No `PapyrusCompiler.exe` is not the same as no Creation Kit *install*. The
built-in engine still needs the vanilla base script *sources* on the import path to resolve `Form`,
`ObjectReference`, `Actor` and the rest, and those ship with the CK rather than in the game archives.
That is a far weaker requirement -- they are plain `.psc` text, widely redistributed, and not
Windows-only, whereas the compiler is a non-redistributable Windows binary -- so this compiles on
Linux, in CI, or anywhere the sources are lying around. It does not compile out of thin air. Point
`PAPYRUS_BASE_IMPORTS`, or **Settings → Papyrus base imports**, at a folder of them; when none is
found, the failure says exactly that rather than listing every vanilla type as unknown.

*Reading and checking* Papyrus source needs nothing installed either. The Papyrus panel's
**Analyze** mode -- syntax errors, an outline, and go-to-definition as you type -- and the
`papyrus_check` / `papyrus_outline` / `papyrus_definition` tools all work with no CK present.
`papyrus_check` resolves names and checks types as well as parsing, and it switches that reporting
off for a file whose parent or import it cannot find, counting those separately, so a clean result
never quietly means "could not tell".

Archives need nothing installed, in either direction. Reading (`archive_list`/`archive_extract`/
`archive_extract_all`) goes through Mutagen; `archive_pack` writes BA2s in process, both General
(sounds/meshes/scripts) and DDS (textures). `Archive2.exe` is only used if you explicitly ask for it
with `use_archive2`, to reproduce its own byte-for-byte output.

---

## What's in the box

```
FO4RecordEditor.exe        the editor + MCP server
web\dist\                  the editor's UI (WebView2)
tools\niftool\             NIF authoring/repair CLI (nif_* tools)
tools\texconv\             DDS conversion fallback (BC6H, signed BCn, float formats)
tools\ckwiki\fallout4\     offline Creation Kit Wiki mirror, for papyrus_function_lookup/papyrus_script_info
tools\audio\               ffmpeg, xWMAEncode, BmlFuzEncode/Decode, for the audio_* tools
mcp.sample.json            copy-paste MCP config
docs\MCP_SETUP.md          the MCP wiring, the tool surface, and the rules
docs\MODKIT21_PULLS.md     tracker for features reviewed/pulled from a comparable tool, and what's still open
docs\internal\            engineering docs: knowledge base, design notes, session history (not shipped)
```

Working on the tool rather than with it? Start at
**[docs/internal/README.md](docs/internal/README.md)**, and read
[docs/internal/KNOWLEDGE.md](docs/internal/KNOWLEDGE.md) before touching plugin or script logic.
Nothing under `docs\internal\` goes into a release: `package.ps1` copies an explicit allowlist.

---

## Licensing

FO4RecordEditor bundles third-party components (Mutagen, niftool/nifly, texconv, WPF-UI and others).
Their licenses and attributions are in **[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)**.

This package contains **no Bethesda assets**. It reads the game files you already own.
