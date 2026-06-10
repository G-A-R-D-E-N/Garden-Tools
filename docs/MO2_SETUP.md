# Running FO4RecordEditor with Mod Organizer 2

FO4RecordEditor reads your load order through Mutagen. To see your **MO2 modlist** (its
virtual Data folder and `plugins.txt`), the editor must run **through MO2** so MO2's
virtual file system hooks it, exactly like you run xEdit through MO2.

## 1. Build the standalone exe

Run `publish.bat` from the project root. It produces a self-contained single-file exe
(no .NET install required) at:

```
FO4RecordEditor\bin\Release\net9.0-windows\win-x64\publish\FO4RecordEditor.exe
```

Copy that exe wherever you like (e.g. next to your other modding tools).

## 2. Add it to MO2

1. In MO2, open the executables dropdown (the gears icon) and choose **Edit...**
2. Click the **+** and **Add from file**, then point it at `FO4RecordEditor.exe`.
3. Give it a title (e.g. "FO4 Record Editor"). Leave the rest default.
4. Click **Apply / OK**.

## 3. Use it

1. Launch **FO4 Record Editor** from the MO2 executables dropdown (so it runs inside the VFS).
2. Click **Load Env**. It builds the environment from your active MO2 profile and shows a
   plugin picker (Select All / Clear All / individual). Pick what you want to load.
3. Open ESP also works for loading a single plugin directly.

## Notes

- If **Load Env** reports "no plugins found", you launched the editor **outside** MO2.
  Always start it from the MO2 executables menu.
- Saving an authored/edited plugin (or a compacted/cleaned one) writes to the editor's
  Output folder by default (configurable in Settings). To drop a result into your modlist,
  point it at the appropriate MO2 mod's folder, or move the file in afterward.
