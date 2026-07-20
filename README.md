# FO4RecordEditor

A Fallout 4 plugin IDE. It is two things in one executable:

- **A desktop editor** — browse, diff, and edit records across your whole load order, scan for
  conflicts, compile and decompile Papyrus, and inspect NIFs. Think xEdit, but with a modern UI.
- **An MCP server** — the same 57 operations, exposed to an AI assistant (Claude Code, or any other
  MCP client). This is the part most people want: you describe the edit, the AI makes it, and the
  plugin is written by [Mutagen](https://github.com/Mutagen-Modding/Mutagen) rather than by hand.

It is built on a patched Mutagen, so it reads and writes real `.esp`/`.esm`/`.esl` binaries. It does
not go through xEdit and does not need xEdit installed.

---

## Install

**1. Install the .NET 9 Desktop Runtime (x64).** This is the only prerequisite.
   Get it here: <https://dotnet.microsoft.com/download/dotnet/9.0> — pick **".NET Desktop Runtime 9.x", x64**.

> The app is deliberately *not* a self-contained single-file exe. Mod Organizer 2's virtual filesystem
> (usvfs) hooks file access, and a self-contained .NET host cannot load its bundled runtime through
> those hooks — the process dies before any managed code runs, so the app "launches" and instantly
> closes with no log and no error. A framework-dependent build loads the runtime from the shared
> install instead, which usvfs handles fine. This is why the runtime is a separate install.

**2. Unzip this package anywhere.** Keep the folder together — the exe loads `web\dist\` and `tools\`
   from beside itself.

**3. Run `FO4RecordEditor.exe`.** That is the GUI. If you only want the AI/MCP integration, you never
   need to open it — but it is the fastest way to confirm the install works.

### Running it under Mod Organizer 2

Add `FO4RecordEditor.exe` as an executable in MO2 (Settings → Executables → `+` → Add from file).
Launching it *through* MO2 is what lets it see your modlist's virtual Data folder, which is what you
want when you are editing a plugin that overrides mods.

---

## Point your AI at it (MCP)

The editor speaks [MCP](https://modelcontextprotocol.io) over stdio. Full detail, including every
tool and the workflow rules that keep the AI from corrupting plugins, is in
**[docs/MCP_SETUP.md](docs/MCP_SETUP.md)** — read that one. The short version:

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
  reads the active profile's `plugins.txt` and reconstructs that exact load order — so the AI sees
  what the game sees, conflicts and all.
- `--data <folder>` is the alternative: a plain `Data` folder, for a vanilla (non-MO2) setup.

Use one or the other. Then restart your AI client, or run `/mcp reconnect` in Claude Code.

---

## Configuration

Settings live in `%APPDATA%\FO4RecordEditor\settings.json`, written by the GUI's settings panel. Every
path below is optional and auto-detected when blank — set one only to override the detection.

| Key | What it points at | Detected from |
|---|---|---|
| `fallout4Path` | Fallout 4 install root | Registry, then the usual Steam library paths |
| `papyrusCompilerPath` | The Creation Kit's `PapyrusCompiler.exe` | `<Fallout 4>\Papyrus Compiler\` |
| `papyrusBaseImports` | Base script roots, `;`-separated | `<Fallout 4>\Data\Scripts\Source\Base` |
| `niftoolPath` | `niftool.exe` | The bundled copy in `tools\niftool\` |
| `texconvPath` | `Texconvx64.exe` | The bundled copy in `tools\texconv\` |
| `mo2InstancePath` | MO2 instance | The `--mo2` argument |
| `dataFolder` | A plain `Data` folder | The `--data` argument |

Each also honours an environment variable, which wins over the settings file:
`NIFTOOL_PATH`, `TEXCONV_PATH`, `PAPYRUS_COMPILER_PATH`, `PAPYRUS_BASE_IMPORTS`.

### Papyrus compiling needs the Creation Kit

`compile_papyrus` shells out to Bethesda's `PapyrusCompiler.exe`, which ships with the **Creation
Kit** and is not redistributable — so it is not in this package. Install the CK (it is free, on Steam)
and the compiler and its base scripts are found automatically. Everything else in the tool works
without it.

---

## What's in the box

```
FO4RecordEditor.exe        the editor + MCP server
web\dist\                  the editor's UI (WebView2)
tools\niftool\             NIF authoring/repair CLI (nif_* tools)
tools\texconv\             DDS conversion, for texture previews
mcp.sample.json            copy-paste MCP config
docs\MCP_SETUP.md          the MCP wiring, the tool surface, and the rules
```

---

## Licensing

FO4RecordEditor bundles third-party components (Mutagen, niftool/nifly, texconv, WPF-UI and others).
Their licenses and attributions are in **[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)**.

This package contains **no Bethesda assets**. It reads the game files you already own.
