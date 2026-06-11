# Using FO4RecordEditor with Mod Organizer 2

**The editor cannot run *through* MO2.** It is a .NET 9 app, and MO2's virtual file system
(usvfs) kills the .NET runtime on injection — the process is created and hooked, but dies
before any managed code runs (verified from MO2's `usvfs-*.log` + the app's own startup log).
This is not a build or settings problem; the app runs fine everywhere else.

Instead, the editor reads your **MO2 modlist directly from disk** and runs **standalone** — no
MO2 launch, no VFS. This is the supported workflow.

## 1. Build the exe

Run `publish.bat` from the project root. It produces a framework-dependent folder build at:

```
FO4RecordEditor\bin\Release\net9.0-windows\win-x64\publish\FO4RecordEditor.exe
```

Requires the **.NET 9 Desktop Runtime** (x64): https://dotnet.microsoft.com/download/dotnet/9.0
Copy the whole `publish` folder wherever you like (keep the exe with its files).

## 2. Load your modlist (Open MO2)

1. Double-click `FO4RecordEditor.exe` (run it normally — **do not** add it to MO2).
2. Click **Open MO2** in the Explorer toolbar.
3. Pick your **MO2 instance folder** — the one containing `mods\`, `profiles\`, and
   `ModOrganizer.ini` (e.g. `D:\Games\ModlistDownloads`).
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
