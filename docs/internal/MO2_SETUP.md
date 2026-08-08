# Using FO4RecordEditor with Mod Organizer 2

> ### The workflow: launch through MO2 **once**, then work outside it
>
> Resolved by the user 2026-07-16. This doc previously claimed the editor *"cannot run through MO2"*
> — **that claim is obsolete** and has been removed. It was true of a **self-contained** build, which
> cannot load its bundled .NET runtime through usvfs's hooks and dies before any managed code runs.
> The current build is **framework-dependent** (verified: `publish\FO4RecordEditor.runtimeconfig.json`
> declares `Microsoft.NETCore.App 9.0.0` and ships no `coreclr.dll`/`hostfxr.dll`), so it loads the
> runtime from the shared install, which usvfs handles.
>
> 1. **Launch it through MO2 once.** That is what lets it see the modlist's virtual Data folder and
>    resolve the real load order.
> 2. **After that, run it standalone.** The instance path is remembered in
>    `%APPDATA%\FO4RecordEditor\settings.json` (`Mo2InstancePath`), so plugins open fine outside MO2.
>
> **The MO2 entry was broken until 2026-07-16** — and this, not usvfs, is what a launch attempt would
> have hit. `ModOrganizer.ini` executable #23 "FO4RecordEditor" pointed at
> `D:\Projects\FO4RecordEditor\...\run.bat`, a path that no longer exists (the tool moved to
> `E:\F4SE OG\Tools\PluginEditTool\`). It now points at the published exe. Backup:
> `ModOrganizer.ini.bak-20260716-fo4re`.
>
> Note `run.bat` is just `dotnet run` — a dev helper that rebuilds from source, needs the SDK, and
> opens a console. **Do not point MO2 at it.** MO2 should launch
> `...\bin\Release\net9.0-windows\win-x64\publish\FO4RecordEditor.exe` directly.
>
> Caveat: that publish is from 2026-07-14 and predates `create_cell`/`create_placed_object`. It cannot
> be refreshed while MCP servers hold the DLLs (see the MSB3021 note in [KNOWLEDGE.md](KNOWLEDGE.md)).

The **Open MO2** workflow below reads your modlist directly off disk and runs standalone — no VFS. It
is the day-to-day path once the one-time MO2 launch above is done.

## 1. Build the exe

Run `publish.bat` from the project root. It produces a framework-dependent folder build at:

```
FO4RecordEditor\bin\Release\net9.0-windows\win-x64\publish\FO4RecordEditor.exe
```

Requires the **.NET 9 Desktop Runtime** (x64): https://dotnet.microsoft.com/download/dotnet/9.0
Copy the whole `publish` folder wherever you like (keep the exe with its files).

## 2. Load your modlist (Open MO2)

1. Run `FO4RecordEditor.exe` — through MO2 for the one-time launch above, standalone thereafter.
2. Click **Open MO2** in the Explorer toolbar.
3. Pick your **MO2 instance folder** — the one containing `mods\`, `profiles\`, and
   `ModOrganizer.ini`. Here that is **`E:\Modlists\Fallen World Alpha 2`** — *not*
   `D:\Games\ModlistDownloads`, which is broken; see [[project_live_modlist]].
4. The editor reads the active profile's `plugins.txt` for the load order and resolves each
   plugin from `overwrite\` → mods (by `modlist.txt` priority) → the game `Data` folder, then
   builds the load order. Pick which plugins to open in the plugin picker.

The active profile and game path come from `ModOrganizer.ini` automatically. The chosen
instance folder is remembered for next time.

**Open ESP** still works for loading a single plugin file directly.

## Notes

- If **Open MO2** reports "no plugins loaded", confirm the profile's `plugins.txt` exists and
  that `gamePath` in `ModOrganizer.ini` points at your game install (its `Data` subfolder holds
  the vanilla/DLC/CC masters).
- A few plugins may report as "could not be resolved" if their file isn't found in any enabled
  mod or the game Data folder; those are skipped and listed in the status line.
- **Data folder override** (Settings → *Game Data Folder*): an alternative to Open MO2. Point it
  at a single real Data folder (e.g. a fully deployed/merged modlist) and use **Load Env**. Leave
  it blank for normal auto-detect.
- Saving an authored/edited plugin writes to the editor's Output folder by default (configurable
  in Settings). To drop a result into your modlist, point it at the appropriate MO2 mod's folder,
  or move the file in afterward.
- **Two settings still point at the broken `D:\Games\ModlistDownloads`** (checked 2026-07-16):
  `DataFolder` = `...\Stock Folder` and `OutputFolder` = `...\Tools\FO4Editor\Output`, in
  `%APPDATA%\FO4RecordEditor\settings.json`. `Mo2InstancePath` is correctly on `E:`, and that is what
  the `--mo2` path uses, so these are inert *today* — but `DataFolder` is the fallback the **Load Env**
  button and `--data` use, so it will resolve against a dead tree the moment either is used. Repoint
  both at `E:` before relying on them. See [[project_live_modlist]].
