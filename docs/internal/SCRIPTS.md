# PluginEditTool Scripts

Three PowerShell helpers wrap the serialize -> edit -> compile -> deploy loop so we
stop retyping paths. All share `_config.ps1` (Spriggit 0.40.1 / `Spriggit.Json.Fallout4`,
modlist at `E:\Modlists\Fallen World Alpha 2\mods`). Verified working.

Run from `E:\F4SE OG\Tools\PluginEditTool`.

## 1. serialize.ps1 - plugin -> JSON tree
```powershell
.\serialize.ps1 -Plugin "MyMod.esp"            # finds it under the modlist -> repos\MyMod\
.\serialize.ps1 -Plugin "E:\path\Some.esp" -Out "repos\Some"
```
`-Plugin` accepts an exact filename, a partial name (searched across the 713
plugins), or a full path. Output defaults to `repos\<name>\`.

## 2. compile.ps1 - JSON tree -> .esp
```powershell
.\compile.ps1 -Src "repos\MyMod"               # -> repos\MyMod.esp
.\compile.ps1 -Src "repos\MyMod" -Out "build\MyMod.esp"
```
Calls the cached translation exe `create-plugin` directly (bypasses the broken
`spriggit` wrapper). Auto-detects whether `RecordData.json` is at the tree root or
inside an inner `<ModName>.esp\` folder. Warns if output is under 5 KB (likely failed).

## 3. deploy.ps1 - .esp -> MO2 mod folder
```powershell
.\deploy.ps1 -Esp "repos\MyMod.esp" -ModFolder "MyMod"
.\deploy.ps1 -Esp "repos\MyMod.esp" -ModFolder "E:\Modlists\Fallen World Alpha 2\mods\Some Mod"
```
Targeted copy only. Never deletes the mod folder. Mod folder must already exist.

## Typical loop
```powershell
.\serialize.ps1 -Plugin "MyMod.esp"
#   ... edit / script the JSON under repos\MyMod\ ...
.\compile.ps1   -Src "repos\MyMod"
.\deploy.ps1    -Esp "repos\MyMod.esp" -ModFolder "MyMod"
#   ... test in game ...
```

## Notes
- `repos\` is the working area (one subfolder per plugin); git-init it if you want diffs.
- ESL plugins risk master-FormID corruption through Spriggit on FO4 - for ESL-heavy
  patches use the xEdit Pascal route instead (see [SPRIGGIT.md](SPRIGGIT.md)).
- If the cached compile exe is ever missing, `compile.ps1` falls back to the
  repo-built exe automatically; running `serialize.ps1` once also repopulates the cache.
