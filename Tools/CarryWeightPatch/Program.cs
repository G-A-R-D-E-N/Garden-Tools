using Mutagen.Bethesda;
using Mutagen.Bethesda.Fallout4;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Order;
using Mutagen.Bethesda.Plugins.Records;

// Phase 1: dump every carry-weight / strength game setting in Fallout4.esm so we
// know the real EditorIDs + current values before authoring overrides.
// Run:  dotnet run -c Release -- dump [path-to-Fallout4.esm]
//       dotnet run -c Release -- patch [path-to-Fallout4.esm] [out.esp]

var cmd  = args.Length > 0 ? args[0].ToLowerInvariant() : "dump";

// makegmst <outEsp> <EditorID=value> [<EditorID=value> ...]
// Authors a tiny override plugin against Fallout4.esm setting the given float GMSTs.
if (cmd == "makegmst")
{
    var srcEsm = @"D:\Games\ModlistDownloads\Stock Folder\Data\Fallout4.esm";
    var master = Fallout4Mod.CreateFromBinaryOverlay(ModPath.FromPath(srcEsm), Fallout4Release.Fallout4);
    var outEsp = args[1];
    var want = args.Skip(2).Select(a => a.Split('=')).ToDictionary(
        p => p[0], p => float.Parse(p[1], System.Globalization.CultureInfo.InvariantCulture),
        StringComparer.OrdinalIgnoreCase);

    var mod = new Fallout4Mod(ModKey.FromNameAndExtension(Path.GetFileName(outEsp)), Fallout4Release.Fallout4);
    int n = 0;
    foreach (var g in master.GameSettings)
    {
        if (g.EditorID == null || !want.TryGetValue(g.EditorID, out var v)) continue;
        if (g is not IGameSettingFloatGetter) { Console.WriteLine($"  SKIP {g.EditorID}: not float"); continue; }
        ((GameSettingFloat)mod.GameSettings.GetOrAddAsOverride(g)).Data = v;
        Console.WriteLine($"  override {g.EditorID} = {v}");
        n++;
    }
    if (n == 0) { Console.WriteLine("No matching GMSTs found — nothing written."); return; }
    Directory.CreateDirectory(Path.GetDirectoryName(outEsp)!);
    mod.WriteToBinary(outEsp);
    Console.WriteLine($"\nWrote {n} override(s) -> {outEsp}");
    return;
}

// findform <esp/esm> <editorIdSubstring> : print FormID + type of matching records.
if (cmd == "findform")
{
    var m = Fallout4Mod.CreateFromBinaryOverlay(ModPath.FromPath(args[1]), Fallout4Release.Fallout4);
    var sub = args[2];
    foreach (var r in m.EnumerateMajorRecords())
        if (r.EditorID != null && r.EditorID.Contains(sub, StringComparison.OrdinalIgnoreCase))
            Console.WriteLine($"  {r.FormKey.ID:X6}  {r.GetType().Name,-20}  {r.EditorID}");
    return;
}

// lockdump <esp> : dump PERK + QUST records, their VMAD scripts/fragments/properties.
if (cmd == "lockdump")
{
    var esp = Fallout4Mod.CreateFromBinaryOverlay(ModPath.FromPath(args[1]), Fallout4Release.Fallout4);
    static void Reflect(string indent, object? o)
    {
        if (o == null) return;
        foreach (var p in o.GetType().GetProperties())
        {
            object? v; try { v = p.GetValue(o); } catch { continue; }
            if (v is System.Collections.IEnumerable e && v is not string)
            {
                int n = 0; foreach (var _ in e) n++;
                Console.WriteLine($"{indent}{p.Name}: [{n} items]");
            }
            else Console.WriteLine($"{indent}{p.Name} = {v}");
        }
    }
    foreach (var q in esp.Quests)
    {
        Console.WriteLine($"\nQUEST {q.FormKey.ID:X6} EditorID={q.EditorID}");
        if (q.VirtualMachineAdapter != null)
            foreach (var s in q.VirtualMachineAdapter.Scripts)
                Console.WriteLine($"  script: {s.Name} flags={s.Flags} props={s.Properties.Count}");
    }
    foreach (var pk in esp.Perks)
    {
        Console.WriteLine($"\nPERK {pk.FormKey.ID:X6} EditorID={pk.EditorID}  effects={pk.Effects.Count}");
        var vm = pk.VirtualMachineAdapter;
        if (vm != null)
        {
            foreach (var s in vm.Scripts)
            {
                Console.WriteLine($"  script: {s.Name} flags={s.Flags}");
                foreach (var p in s.Properties) Console.WriteLine($"    prop: {p.Name} [{p.GetType().Name}]");
            }
            Console.WriteLine("  ScriptFragments:");
            Reflect("    ", vm.ScriptFragments);
            if (vm.ScriptFragments != null)
                foreach (var f in vm.ScriptFragments.Fragments)
                { Console.WriteLine("    -- fragment --"); Reflect("      ", f); }
        }
        foreach (var e in pk.Effects)
            Console.WriteLine($"  effect: {e.GetType().Name}");
    }
    return;
}

// lockbuild : clone the proven Locksmith esp into a renamed FW_Locksmith.esp that
// references OUR recompiled scripts. Keeps the (working) perk entry points + conditions.
if (cmd == "lockbuild")
{
    var inPath  = @"E:\F4SE OG\Non Prisma Projects\Lock\Locksmith.esp";
    var lockOut = @"E:\F4SE OG\Non Prisma Projects\Lock\FWLocksmith\out\FW_Locksmith.esp";
    const string fragScript  = "Fragments:Perks:FWLocksmith_PerkFragment";
    const string startScript = "FWLocksmith_Startup";

    var mod = Fallout4Mod.CreateFromBinary(ModPath.FromPath(inPath), Fallout4Release.Fallout4);

    // Skeleton Key — must be in inventory to use any lock/unlock option.
    var key = mod.MiscItems.AddNew("FW_SkeletonKey");
    key.Name = "Skeleton Key";
    key.Model = new Model { File = "Props\\Key01.nif" };
    key.Weight = 0.5f;
    key.Value = 2000;   // ~2000 caps from vendors

    GlobalShort MkGlob(string eid)
    {
        var g = new GlobalShort(mod.GetNextFormKey(), Fallout4Release.Fallout4) { EditorID = eid, Data = 0 };
        mod.Globals.Add(g);
        return g;
    }
    void AddObjPropKey(IList<ScriptProperty> props, string name, FormKey fk)
    {
        if (props.Any(p => p.Name == name)) return;
        var op = new ScriptObjectProperty { Name = name, Flags = ScriptProperty.Flag.Edited };
        op.Object.SetTo(fk);
        props.Add(op);
    }

    // Live lockpick counter + config/runtime globals (#3 durability, #4 traps, #7 config).
    // These hold sensible DEFAULTS baked into the esp, so the features work on any save
    // with no new game. FW_Locksmith.json only OVERRIDES them when it loads.
    var progress    = MkGlob("FW_LockpickProgress");
    var cfgKeyUses  = MkGlob("FW_Cfg_KeyUses");       cfgKeyUses.Data  = 25;  // uses before key breaks (0 = infinite)
    var cfgTraps    = MkGlob("FW_Cfg_TrapsEnabled");  cfgTraps.Data    = 1;   // 1 = allow rigging traps
    var keyUsesLeft = MkGlob("FW_KeyUsesLeft");       keyUsesLeft.Data = 25;  // runtime charge counter

    // #4 — list of refs the player has rigged to explode.
    var trapped = mod.FormLists.AddNew("FW_TrappedLocks");

    // #1 — lock-tier choice menu.
    var tierMsg = mod.Messages.AddNew("FW_LockTierMsg");
    tierMsg.Name = "Skeleton Key";
    tierMsg.Description = "Secure this lock?";
    tierMsg.Flags = Message.Flag.MessageBox;
    foreach (var t in new[] { "Cancel", "Lock", "Lock & Trap" })
        tierMsg.MenuButtons.Add(new MessageButton { Text = t });

    var explKey = FormKey.Factory("0E574F:Fallout4.esm");   // fragGrenadeExplosion for traps
    var grenadeKey = FormKey.Factory("0EEBED:Fallout4.esm"); // fragGrenade weapon (consumed to rig)
    var startupQuest = mod.Quests.First(q => q.EditorID == "aaLocksmithStartUpQuest");

    // Always-running system quest: config + trap handler. Being new + start-game-enabled,
    // it initializes even on existing saves (no new game needed). Never stops.
    var sysQuest = mod.Quests.AddNew("FW_LocksmithSystem");
    sysQuest.Name = "Locksmith System";
    sysQuest.Data = new Mutagen.Bethesda.Fallout4.QuestData { Flags = Quest.Flag.StartGameEnabled };
    sysQuest.VirtualMachineAdapter = new QuestAdapter
    {
        Version = startupQuest.VirtualMachineAdapter!.Version,
        ObjectFormat = startupQuest.VirtualMachineAdapter!.ObjectFormat,
    };
    var sysScript = new ScriptEntry { Name = "FWLocksmith_System", Flags = ScriptEntry.Flag.Local };
    AddObjProp(sysScript.Properties, "FW_Cfg_KeyUses", cfgKeyUses);
    AddObjProp(sysScript.Properties, "FW_Cfg_TrapsEnabled", cfgTraps);
    AddObjProp(sysScript.Properties, "FW_KeyUsesLeft", keyUsesLeft);
    AddObjProp(sysScript.Properties, "FW_TrappedLocks", trapped);
    AddObjPropKey(sysScript.Properties, "FW_TrapExplosion", explKey);
    sysQuest.VirtualMachineAdapter.Scripts.Add(sysScript);

    static void AddObjProp(IList<ScriptProperty> props, string name, IFallout4MajorRecordGetter target)
    {
        if (props.Any(p => p.Name == name)) return;
        var op = new ScriptObjectProperty { Name = name, Flags = ScriptProperty.Flag.Edited };
        op.Object.SetTo(target);
        props.Add(op);
    }

    foreach (var pk in mod.Perks)
    {
        if (pk.EditorID == "aaLocksmithPerk") pk.EditorID = "FW_LocksmithPerk";
        var sf = pk.VirtualMachineAdapter?.ScriptFragments;
        if (sf != null)
        {
            if (sf.Script != null)
            {
                sf.Script.Name = fragScript;
                AddObjProp(sf.Script.Properties, "FW_SkeletonKey", key);
                AddObjProp(sf.Script.Properties, "FW_LockTierMsg", tierMsg);
                AddObjProp(sf.Script.Properties, "FW_TrapHandler", sysQuest);
                AddObjPropKey(sf.Script.Properties, "FW_FragGrenade", grenadeKey);
                AddObjProp(sf.Script.Properties, "FW_Cfg_KeyUses", cfgKeyUses);
                AddObjProp(sf.Script.Properties, "FW_KeyUsesLeft", keyUsesLeft);
                AddObjProp(sf.Script.Properties, "FW_Cfg_TrapsEnabled", cfgTraps);
            }
            foreach (var f in sf.Fragments) f.ScriptName = fragScript;
        }

        // Don't offer LOCK on wasteland loot containers (ammo boxes, toolboxes, chem boxes):
        // those carry IsChestForStatTracking and are where loot/ammo-search mods put their own
        // activate choice — our LOCK was clobbering it. Mirror the existing Workbench exclusion.
        var kwChest = FormKey.Factory("184462:Fallout4.esm");   // IsChestForStatTracking
        foreach (var eff in pk.Effects.OfType<PerkEntryPointAddActivateChoice>())
        {
            foreach (var pc in eff.Conditions)
            {
                // Only container choices carry a HasKeyword(Workbench) exclusion; doors don't.
                var tmpl = pc.Conditions.FirstOrDefault(c =>
                    (c.Data as IFunctionConditionDataGetter)?.Function == Condition.Function.HasKeyword);
                if (tmpl == null) continue;
                var clone = (Condition)tmpl.DeepCopy();
                ((IFunctionConditionData)clone.Data).ParameterOneRecord.SetTo(kwChest);
                pc.Conditions.Add(clone);
                Console.WriteLine($"  perk: container '{eff.ButtonLabel}' now excludes IsChestForStatTracking (loot boxes)");
                break;
            }
        }

        // Hide LOCK/UNLOCK entirely until the player owns a Skeleton Key (earned via the
        // quest or bought from a trader). Adds GetItemCount(key) >= 1 on the Subject (player)
        // to every activate choice, so there's no prompt clutter before you're a locksmith.
        foreach (var eff in pk.Effects.OfType<PerkEntryPointAddActivateChoice>())
        {
            if (eff.Conditions.Count == 0) continue;
            var tmplC = eff.Conditions[0].Conditions.FirstOrDefault(c =>
                (c.Data as IFunctionConditionDataGetter)?.Function == Condition.Function.GetIsObjectType);
            if (tmplC == null) continue;
            var keyCond = (Condition)tmplC.DeepCopy();
            var fd = (IFunctionConditionData)keyCond.Data;
            fd.Function = Condition.Function.GetItemCount;
            fd.ParameterOneRecord.SetTo(key.FormKey);
            fd.RunOnType = Condition.RunOnType.Subject;
            keyCond.CompareOperator = CompareOperator.GreaterThanOrEqualTo;
            ((ConditionFloat)keyCond).ComparisonValue = 1f;
            eff.Conditions[0].Conditions.Add(keyCond);
            Console.WriteLine($"  perk: '{eff.ButtonLabel}' now requires holding the Skeleton Key");
        }

        Console.WriteLine($"perk -> {pk.EditorID}, frag script -> {fragScript}, +FW_PlayerLocks");
    }

    foreach (var q in mod.Quests)
    {
        if (q.EditorID == "aaLocksmithStartUpQuest") q.EditorID = "FW_LocksmithQuest";
        if (q.VirtualMachineAdapter != null)
            foreach (var s in q.VirtualMachineAdapter.Scripts)
            {
                if (s.Name == "aaLockSmithStartUpQuestScript") s.Name = startScript;
                foreach (var p in s.Properties)
                    if (p.Name == "aaLocksmithPerk") p.Name = "FW_LocksmithPerk";
                // Drop the original mod's stale self-reference property — the simplified
                // FWLocksmith_Startup no longer declares it, so it threw "cannot be
                // initialized" warnings on every load.
                for (int i = s.Properties.Count - 1; i >= 0; i--)
                    if (s.Properties[i].Name == "aaLocksmithStartUpQuest") s.Properties.RemoveAt(i);
                // The Mutagen-authored start-game-enabled quests don't auto-start without a CK
                // .seq, but THIS quest does run on a new game — so boot the trap system from
                // here (FW_EarnQuest is bound below, once it exists).
                if (s.Name == startScript) AddObjProp(s.Properties, "FW_TrapHandler", sysQuest);
            }
        Console.WriteLine($"quest -> {q.EditorID}, script -> {startScript}");
    }

    foreach (var fl in mod.FormLists)
        if (fl.EditorID != null && !fl.EditorID.StartsWith("FW_")) fl.EditorID = "FW_" + fl.EditorID;

    // ── New quest: earn the Skeleton Key in Goodneighbor ───────────────────
    var tmplVm = mod.Quests.First(q => q.EditorID == "FW_LocksmithQuest").VirtualMachineAdapter!;
    var skq = mod.Quests.AddNew("FW_SkeletonKeyQuest");
    skq.Name = "The Locksmith's Key";
    skq.Data = new Mutagen.Bethesda.Fallout4.QuestData { Flags = Quest.Flag.StartGameEnabled | Quest.Flag.RunOnce };
    skq.Objectives.Add(new Mutagen.Bethesda.Fallout4.QuestObjective
    {
        Index = 10,
        DisplayText = "Pick locks to fashion a Skeleton Key (<Global=FW_LockpickProgress>/20)",
    });
    skq.VirtualMachineAdapter = new QuestAdapter
    {
        Version = tmplVm.Version,
        ObjectFormat = tmplVm.ObjectFormat,
    };
    var skqScript = new ScriptEntry { Name = "FW_SkeletonKeyQuest", Flags = ScriptEntry.Flag.Local };
    AddObjProp(skqScript.Properties, "FW_SkeletonKey", key);
    AddObjProp(skqScript.Properties, "FW_LockpickProgress", progress);
    skq.VirtualMachineAdapter.Scripts.Add(skqScript);
    Console.WriteLine($"quest -> FW_SkeletonKeyQuest {skq.FormKey.ID:X6}");

    // Bind the earn quest to the perk fragment (perk boots it on lock-use) AND to the startup
    // quest (which boots it on a new game, since the start-game-enabled quest has no .seq).
    foreach (var pk in mod.Perks)
    {
        var sf2 = pk.VirtualMachineAdapter?.ScriptFragments;
        if (sf2?.Script != null) AddObjProp(sf2.Script.Properties, "FW_EarnQuest", skq);
    }
    foreach (var q in mod.Quests)
        if (q.EditorID == "FW_LocksmithQuest" && q.VirtualMachineAdapter != null)
            foreach (var s in q.VirtualMachineAdapter.Scripts)
                if (s.Name == startScript) AddObjProp(s.Properties, "FW_EarnQuest", skq);

    Console.WriteLine($"Skeleton Key FormID = {key.FormKey.ID:X6}  (value {key.Value})");
    Directory.CreateDirectory(Path.GetDirectoryName(lockOut)!);
    mod.WriteToBinary(lockOut, new Mutagen.Bethesda.Plugins.Binary.Parameters.BinaryWriteParameters
    {
        ModKey = Mutagen.Bethesda.Plugins.Binary.Parameters.ModKeyOption.CorrectToPath
    });
    Console.WriteLine($"\nWrote {lockOut}");
    return;
}

var path = args.Length > 1 ? args[1]
    : @"D:\Games\ModlistDownloads\Stock Folder\Data\Fallout4.esm";

Console.WriteLine($"Loading {path}");
var esm = Fallout4Mod.CreateFromBinaryOverlay(ModPath.FromPath(path), Fallout4Release.Fallout4);

bool M(string? s, params string[] needles) =>
    s != null && needles.Any(n => s.Contains(n, StringComparison.OrdinalIgnoreCase));

static string ValOf(IGameSettingGetter g) => g switch
{
    IGameSettingFloatGetter f => $"float {f.Data?.ToString() ?? "null"}",
    IGameSettingIntGetter i   => $"int   {i.Data?.ToString() ?? "null"}",
    IGameSettingBoolGetter b  => $"bool  {b.Data?.ToString() ?? "null"}",
    IGameSettingStringGetter s => $"str   {s.Data?.String ?? "null"}",
    _ => "unknown"
};

Console.WriteLine("\n=== GMST matching carry / strength / encumb / avd ===");
foreach (var g in esm.GameSettings)
    if (M(g.EditorID, "carry", "strength", "encumb", "avdcarry", "actorstrength"))
        Console.WriteLine($"  {g.FormKey.ID:X6}  {g.EditorID,-32}  {ValOf(g)}");

if (cmd == "dump")
{
    Console.WriteLine("\nDump done.");
    return;
}

// contkw <pluginPath> <substr> : find CONT records whose EditorID/Name contains substr, list keywords.
if (cmd == "contkw")
{
    var cpath = args[1];
    var sub = args.Length > 2 ? args[2] : "ammo";
    var cm = Fallout4Mod.CreateFromBinaryOverlay(ModPath.FromPath(cpath), Fallout4Release.Fallout4);
    Console.WriteLine($"\n=== CONT matching '{sub}' in {Path.GetFileName(cpath)} ===");
    int shown = 0;
    foreach (var c in cm.Containers)
    {
        var eid = c.EditorID ?? "";
        var nm = c.Name?.String ?? "";
        if (!eid.Contains(sub, StringComparison.OrdinalIgnoreCase) && !nm.Contains(sub, StringComparison.OrdinalIgnoreCase)) continue;
        if (shown++ > 25) break;
        var kws = c.Keywords?.Select(k => k.FormKey.ID.ToString("X6")).ToList() ?? new();
        Console.WriteLine($"  {c.FormKey.ID:X6}  {eid,-30} \"{nm}\"  kw=[{string.Join(",", kws)}]");
    }
    Console.WriteLine("\nContkw done.");
    return;
}

// gunsmithscan [instance] [profile] : load the whole MO2 modlist, find every weapon-mod recipe
// (HasPerk GunNut 1-4) and report the component-count distribution (preview before patching).
if (cmd == "gunsmithscan" || cmd == "gunsmithpatch")
{
    var instance = args.Length > 1 ? args[1] : @"D:\Games\ModlistDownloads";
    var profile  = args.Length > 2 ? args[2] : "Fallen World";
    var info = Mo2Loader.ReadInstanceInfo(instance);
    var dataFolder = !string.IsNullOrEmpty(info.GameDataFolder) ? info.GameDataFolder
        : @"D:\Games\ModlistDownloads\Stock Folder\Data";
    Console.WriteLine($"Instance={instance}  Profile={profile}  Data={dataFolder}");
    var (env, _) = Mo2Loader.Load(instance, profile, dataFolder);

    var gunNut = new HashSet<FormKey> {
        FormKey.Factory("04A0DA:Fallout4.esm"), FormKey.Factory("04A0DB:Fallout4.esm"),
        FormKey.Factory("04A0DC:Fallout4.esm"), FormKey.Factory("16578E:Fallout4.esm"),
    };
    // component count -> required Gunsmith skill (floor of 10 so even trivial 1-part mods gate).
    static int SkillFor(int comps) => comps <= 1 ? 10 : comps == 2 ? 15 : comps == 3 ? 30 : comps == 4 ? 45 : 60;

    int total = 0;
    var byComp = new SortedDictionary<int, int>();
    var byTier = new SortedDictionary<int, int>();
    foreach (var cobj in env.LoadOrder.PriorityOrder.WinningOverrides<IConstructibleObjectGetter>())
    {
        bool isGun = cobj.Conditions.Any(c => c.Data is IFunctionConditionDataGetter fd
            && fd.Function == Condition.Function.HasPerk && gunNut.Contains(fd.ParameterOneRecord.FormKey));
        if (!isGun) continue;
        total++;
        int comps = cobj.Components?.Count ?? 0;
        byComp.TryGetValue(comps, out var n); byComp[comps] = n + 1;
        int sk = SkillFor(comps); byTier.TryGetValue(sk, out var t); byTier[sk] = t + 1;
    }
    Console.WriteLine($"\n=== Gun-mod recipes (gate on Gun Nut): {total} ===");
    Console.WriteLine("-- by component count --");
    foreach (var kv in byComp) Console.WriteLine($"  {kv.Key} comps -> {kv.Value}");
    Console.WriteLine("-- mapped to required Gunsmith skill --");
    foreach (var kv in byTier) Console.WriteLine($"  Gunsmith >= {kv.Key,2} -> {kv.Value} recipes");
    if (cmd == "gunsmithscan") return;

    // ── gunsmithpatch: write the override esp ──────────────────────────────────
    // Gate by Gun Nut RANK (HasPerk works cleanly; FO4 can't condition recipes on a custom skill
    // AV). S7's VPO grants Gun Nut ranks as the Gunsmith skill levels, so rank == skill tier.
    // component count -> target Gun Nut rank perk.
    FormKey RankFor(int comps) => comps <= 2 ? FormKey.Factory("04A0DA:Fallout4.esm")   // GunNut1
        : comps == 3 ? FormKey.Factory("04A0DB:Fallout4.esm")                            // GunNut2
        : comps == 4 ? FormKey.Factory("04A0DC:Fallout4.esm")                            // GunNut3
        : FormKey.Factory("16578E:Fallout4.esm");                                        // GunNut4 (5+)

    var gpatch = new Fallout4Mod(ModKey.FromNameAndExtension("FW_GunsmithGating.esp"), Fallout4Release.Fallout4);
    int patched = 0, skipped = 0;
    var tierCount = new SortedDictionary<int, int>();
    foreach (var cobj in env.LoadOrder.PriorityOrder.WinningOverrides<IConstructibleObjectGetter>())
    {
        if (!cobj.Conditions.Any(c => c.Data is IFunctionConditionDataGetter fd
            && fd.Function == Condition.Function.HasPerk && gunNut.Contains(fd.ParameterOneRecord.FormKey))) continue;

        var ov = gpatch.ConstructibleObjects.GetOrAddAsOverride(cobj);   // deep-copies EVERYTHING
        var rank = RankFor(cobj.Components?.Count ?? 0);

        // Retarget each Gun Nut condition to the tier's rank (keep Function=HasPerk so the form
        // param remaps correctly). All other conditions/components/data are left untouched.
        bool kept = false;
        for (int i = ov.Conditions.Count - 1; i >= 0; i--)
        {
            if (ov.Conditions[i].Data is IFunctionConditionDataGetter fd
                && fd.Function == Condition.Function.HasPerk && gunNut.Contains(fd.ParameterOneRecord.FormKey))
            {
                if (!kept) { ((IFunctionConditionData)ov.Conditions[i].Data).ParameterOneRecord.SetTo(rank); kept = true; }
                else ov.Conditions.RemoveAt(i);   // collapse duplicate GunNut gates into one
            }
        }
        int rk = rank.ID == 0x04A0DA ? 1 : rank.ID == 0x04A0DB ? 2 : rank.ID == 0x04A0DC ? 3 : 4;
        tierCount.TryGetValue(rk, out var tc); tierCount[rk] = tc + 1;
        patched++;
    }
    Console.WriteLine("-- mapped to Gun Nut rank (= Gunsmith skill tier via VPO) --");
    foreach (var kv in tierCount) Console.WriteLine($"  Gun Nut {kv.Key} -> {kv.Value} recipes");

    Console.WriteLine($"\nPatched {patched} recipes ({skipped} skipped).");
    var outDir = @"D:\Games\ModlistDownloads\mods\FW Gunsmith Gating";
    Directory.CreateDirectory(outDir);
    var outEsp = Path.Combine(outDir, "FW_GunsmithGating.esp");
    gpatch.WriteToBinary(outEsp, new Mutagen.Bethesda.Plugins.Binary.Parameters.BinaryWriteParameters
    {
        ModKey = Mutagen.Bethesda.Plugins.Binary.Parameters.ModKeyOption.CorrectToPath,
    });
    Console.WriteLine($"Wrote {outEsp}");
    return;
}

// cobjdump <pluginPath> <substr> : dump CONSTRUCTIBLE recipes whose EditorID/created-item
// matches substr, with bench keyword + conditions (to find schematic/perk gates).
if (cmd == "cobjdump")
{
    var cpath = args[1];
    var sub = args.Length > 2 ? args[2] : "radroach";
    var cm = Fallout4Mod.CreateFromBinaryOverlay(ModPath.FromPath(cpath), Fallout4Release.Fallout4);
    Console.WriteLine($"\n=== COBJ matching '{sub}' in {Path.GetFileName(cpath)} ===");
    foreach (var co in cm.ConstructibleObjects)
    {
        var eid = co.EditorID ?? "";
        var cname = co.CreatedObject.FormKey.ToString();
        if (!eid.Contains(sub, StringComparison.OrdinalIgnoreCase) &&
            !cname.Contains(sub, StringComparison.OrdinalIgnoreCase)) continue;
        Console.WriteLine($"  COBJ {co.FormKey.ID:X6} {eid}  -> created={cname}  bench={co.WorkbenchKeyword.FormKey}  conds={co.Conditions.Count}");
        foreach (var c in co.Conditions)
            if (c is IConditionFloatGetter cf && cf.Data is IFunctionConditionDataGetter fd)
                Console.WriteLine($"      cond: {fd.Function} p1={fd.ParameterOneRecord.FormKey} op={cf.CompareOperator} cmp={cf.ComparisonValue}");
    }
    Console.WriteLine("\nCobjdump done.");
    return;
}

// objdump <plugin> <hexFormId> : detail one record (type, model, keywords, COBJ fields, scripts).
if (cmd == "objdump")
{
    var opath = args[1];
    uint id = Convert.ToUInt32(args[2], 16);
    var m = Fallout4Mod.CreateFromBinaryOverlay(ModPath.FromPath(opath), Fallout4Release.Fallout4);
    foreach (var rec in m.EnumerateMajorRecords())
    {
        if (rec.FormKey.ID != id) continue;
        Console.WriteLine($"Record {rec.FormKey} type={rec.GetType().Name} EditorID={rec.EditorID}");
        if (rec is IModeledGetter md && md.Model != null) Console.WriteLine($"  Model={md.Model.File}");
        if (rec is Mutagen.Bethesda.Plugins.Aspects.IKeywordedGetter kw && kw.Keywords != null)
            foreach (var k in kw.Keywords) Console.WriteLine($"  KWDA {k.FormKey}");
        if (rec is IConstructibleObjectGetter co)
        {
            Console.WriteLine($"  COBJ created={co.CreatedObject.FormKey} bench={co.WorkbenchKeyword.FormKey}");
            if (co.Categories != null) foreach (var c in co.Categories) Console.WriteLine($"  CATEGORY {c.FormKey}");
            if (co.Components != null) foreach (var comp in co.Components) Console.WriteLine($"  COMP {comp.Component.FormKey} x{comp.Count}");
        }
        try {
            dynamic dyn = rec;
            var vmad = dyn.VirtualMachineAdapter;
            if (vmad != null) foreach (var s in vmad.Scripts) Console.WriteLine($"  SCRIPT {s.Name}");
        } catch { }
        Console.WriteLine("objdump done."); return;
    }
    Console.WriteLine("not found"); return;
}

// amazonbuild : author VaultAzon.esp — a workshop-buildable activator that opens the
// VaultAzon storefront on activation. Clones the vanilla buildable-terminal recipe
// (workshop_co_Terminal 16A387 -> WorkshopTerminal 0CEA77) but makes the created object
// an ACTI with our OnActivate script instead of a TERM, so it calls PrismaAmazon.OpenStore().
if (cmd == "amazonbuild")
{
    var esp = new Fallout4Mod(ModKey.FromNameAndExtension("VaultAzon.esp"), Fallout4Release.Fallout4);

    // Created FIRST so it gets local FormID 0x800 — the DLL looks it up by that id to flag
    // a pending delivery (the terminal script polls it and flies the vertibird ambiance).
    var deliverGlob = new GlobalShort(esp.GetNextFormKey(), Fallout4Release.Fallout4) { EditorID = "VaultAzon_DeliverPending", Data = 0 };
    esp.Globals.Add(deliverGlob);

    var acti = esp.Activators.AddNew("VaultAzon_Terminal");
    acti.Name = "VaultAzon Terminal";
    acti.Model = new Model { File = "Furniture\\Terminals\\TerminalOnImprovisedWorkshop.nif" };
    // Same workshop keywords the vanilla buildable terminal carries (placement, snap, power, scrap).
    acti.Keywords = new Noggog.ExtendedList<IFormLinkGetter<IKeywordGetter>>();
    foreach (var kw in new[] { "03037E", "06D5BB", "0FCB12", "054BA4", "0A56D7", "0286E6", "224A6B" })
        acti.Keywords.Add(new FormLink<IKeywordGetter>(FormKey.Factory(kw + ":Fallout4.esm")));
    acti.VirtualMachineAdapter = new VirtualMachineAdapter { Version = 6, ObjectFormat = 2 };
    var termScript = new ScriptEntry { Name = "VaultAzonTerminalScript", Flags = ScriptEntry.Flag.Local };
    void ObjProp(string name, FormKey fk)
    {
        var op = new ScriptObjectProperty { Name = name, Flags = ScriptProperty.Flag.Edited };
        op.Object.SetTo(fk);
        termScript.Properties.Add(op);
    }
    ObjProp("VaultAzon_DeliverPending", deliverGlob.FormKey);
    ObjProp("VaultAzon_VertibirdModel", FormKey.Factory("028A1D:Fallout4.esm"));   // Vertibird01 STATIC (no physics, won't fall)
    ObjProp("VaultAzon_Smoke",          FormKey.Factory("05691A:Fallout4.esm"));   // BoSVertibirdGrenadeSmoke
    ObjProp("VaultAzon_FlySound",       FormKey.Factory("2388AC:Fallout4.esm"));   // vertibird engine SNDR
    acti.VirtualMachineAdapter.Scripts.Add(termScript);

    var cobj = esp.ConstructibleObjects.AddNew("co_VaultAzon_Terminal");
    cobj.CreatedObject.SetTo(acti);
    cobj.WorkbenchKeyword.SetTo(FormKey.Factory("05A0CA:Fallout4.esm"));   // workshop bench
    cobj.Categories = new Noggog.ExtendedList<IFormLinkGetter<IKeywordGetter>>
        { new FormLink<IKeywordGetter>(FormKey.Factory("16AC69:Fallout4.esm")) };   // terminal menu category
    cobj.Components = new Noggog.ExtendedList<ConstructibleObjectComponent>();
    void Comp(string fk, uint n) { var c = new ConstructibleObjectComponent { Count = n }; c.Component.SetTo(FormKey.Factory(fk + ":Fallout4.esm")); cobj.Components.Add(c); }
    Comp("01FAB9", 2); Comp("01FA9C", 2); Comp("01FA9B", 2); Comp("01FA91", 4);   // same build cost as vanilla terminal

    var outDir = @"D:\Games\ModlistDownloads\mods\VaultAzon";
    Directory.CreateDirectory(outDir);
    var outEsp = Path.Combine(outDir, "VaultAzon.esp");
    esp.WriteToBinary(outEsp, new Mutagen.Bethesda.Plugins.Binary.Parameters.BinaryWriteParameters
    {
        ModKey = Mutagen.Bethesda.Plugins.Binary.Parameters.ModKeyOption.CorrectToPath,
    });
    Console.WriteLine($"Wrote {outEsp}  (GLOB {deliverGlob.FormKey.ID:X6}, ACTI {acti.FormKey.ID:X6}, COBJ {cobj.FormKey.ID:X6})");
    return;
}

// amazoncatalog [instance] [profile] [valueFloor] [maxItems] : scan the whole MO2 modlist and
// generate the VaultAzon storefront catalog (every named, valued item) — same modlist-scan
// approach as the crafting mod's data generation. Writes catalog.generated.js to the view.
if (cmd == "amazoncatalog")
{
    var instance = args.Length > 1 ? args[1] : @"D:\Games\ModlistDownloads";
    var profile  = args.Length > 2 ? args[2] : "Fallen World";
    int floor    = args.Length > 3 && int.TryParse(args[3], out var fv) ? fv : 5;
    int maxItems = args.Length > 4 && int.TryParse(args[4], out var mv) ? mv : 1500;
    var info = Mo2Loader.ReadInstanceInfo(instance);
    var dataFolder = !string.IsNullOrEmpty(info.GameDataFolder) ? info.GameDataFolder : @"D:\Games\ModlistDownloads\Stock Folder\Data";
    var (env2, _) = Mo2Loader.Load(instance, profile, dataFolder);

    string[] sellers = {
        "Diamond City Arms", "Bunker Hill Outfitters", "Honest Abe's Salvage", "Med-Tek Now",
        "Atom Cats Drip Co.", "Wild Wasteland Provisions", "Vault-Tec Surplus", "Goodneighbor Goods",
        "Crater Springs", "Wasteland Knick-Knacks", "Big Boom Logistics LLC", "Sturges Approved Goods",
        "RobCo Liquidation", "Nuka-Corp Official Store", "Trashcan Carla's Caravan", "Drumlin Diner Provisions",
    };
    static uint Hash(string s) { uint h = 2166136261; foreach (var c in s) { h ^= c; h *= 16777619; } return h; }
    static string Slug(string s) { var sb = new System.Text.StringBuilder(); foreach (var c in s.ToLowerInvariant()) sb.Append(char.IsLetterOrDigit(c) ? c : '-'); return sb.ToString().Trim('-'); }
    static bool IsJunkEid(string? eid) => eid != null && (eid.Contains("test", StringComparison.OrdinalIgnoreCase)
        || eid.Contains("debug", StringComparison.OrdinalIgnoreCase) || eid.Contains("donotuse", StringComparison.OrdinalIgnoreCase)
        || eid.Contains("zzz", StringComparison.OrdinalIgnoreCase) || eid.Contains("delete", StringComparison.OrdinalIgnoreCase));

    var byName = new Dictionary<string, (string name, string cat, string icon, uint value, string eid)>(StringComparer.OrdinalIgnoreCase);
    void Consider(string? name, uint? value, string? eid, string cat, string icon)
    {
        if (string.IsNullOrWhiteSpace(name) || value is null || value < floor) return;
        if (IsJunkEid(eid)) return;
        var key = name.Trim();
        if (byName.TryGetValue(key, out var ex) && ex.value >= value) return;   // keep the pricier duplicate
        byName[key] = (key, cat, icon, value.Value, eid ?? "");
    }
    static uint? Val(object rec) { var p = rec.GetType().GetProperty("Value")?.GetValue(rec); if (p == null) return null; try { return Convert.ToUInt32(p); } catch { return null; } }
    static string? Nm(object rec) => (rec as Mutagen.Bethesda.Plugins.Aspects.INamedGetter)?.Name;

    foreach (var r in env2.LoadOrder.PriorityOrder.WinningOverrides<IWeaponGetter>())     Consider(Nm(r), Val(r), r.EditorID, "Weapons", "🔫");
    foreach (var r in env2.LoadOrder.PriorityOrder.WinningOverrides<IArmorGetter>())      Consider(Nm(r), Val(r), r.EditorID, "Apparel", "🦺");
    foreach (var r in env2.LoadOrder.PriorityOrder.WinningOverrides<IAmmunitionGetter>()) Consider(Nm(r), Val(r), r.EditorID, "Ammo", "🔩");
    foreach (var r in env2.LoadOrder.PriorityOrder.WinningOverrides<IBookGetter>())       Consider(Nm(r), Val(r), r.EditorID, "Books", "📖");
    foreach (var r in env2.LoadOrder.PriorityOrder.WinningOverrides<IMiscItemGetter>())   Consider(Nm(r), Val(r), r.EditorID, "Junk", "🧩");
    foreach (var r in env2.LoadOrder.PriorityOrder.WinningOverrides<IIngestibleGetter>())
    {
        bool food = (r.GetType().GetProperty("Flags")?.GetValue(r)?.ToString() ?? "").Contains("Food", StringComparison.OrdinalIgnoreCase);
        Consider(Nm(r), Val(r), r.EditorID, food ? "Food & Drink" : "Aid", food ? "🥫" : "💊");
    }

    // Balance the store: take the top-N (by value) within each category instead of a single
    // value-desc cut (which would otherwise drown the store in high-value junk/components).
    var catCaps = new Dictionary<string, int> {
        ["Weapons"] = 450, ["Apparel"] = 400, ["Aid"] = 200, ["Food & Drink"] = 250,
        ["Ammo"] = 120, ["Books"] = 120, ["Junk"] = 250,
    };
    var picked = byName.Values
        .GroupBy(v => v.cat)
        .SelectMany(g => g.OrderByDescending(v => v.value).Take(catCaps.TryGetValue(g.Key, out var c) ? c : 150))
        .OrderByDescending(v => v.value)
        .Take(maxItems)
        .ToList();
    var catalog = picked.Select(it =>
    {
        var h = Hash(it.name);
        double rating = 3.0 + (h % 21) / 10.0;
        var price = (int)it.value;
        var listPrice = price + (int)(price * ((h % 5) + 1) * 0.05);
        return new {
            id = Slug(it.name) + "-" + (h & 0xFFFFFF).ToString("x6"),
            name = it.name,
            category = it.cat,
            price,
            listPrice,
            icon = it.icon,
            wiki = it.name,
            rating = Math.Round(rating, 1),
            prime = (h % 3) == 0,
            seller = sellers[h % sellers.Length],
            blurb = $"Genuine wasteland {it.cat.ToLowerInvariant()}. Appraised at {price} caps by a man who definitely owns a Pip-Boy. Ships when the brahmin feels like it.",
            tags = (h % 11) == 0 ? new[] { "bestseller" } : System.Array.Empty<string>(),
        };
    }).ToList();

    var json = System.Text.Json.JsonSerializer.Serialize(catalog, new System.Text.Json.JsonSerializerOptions { WriteIndented = false });
    var js = "/* AUTO-GENERATED by CarryWeightPatch `amazoncatalog`. Do not edit by hand. */\n"
           + "window.GENERATED_CATALOG = " + json + ";\n";
    string[] dests = {
        @"E:\F4SE OG\Prisma\UI_Systems\PrismaAmazon\catalog.generated.js",
        @"D:\Games\ModlistDownloads\mods\VaultAzon\PrismaUI_F4\views\Interface\PrismaAmazon\catalog.generated.js",
    };
    foreach (var d in dests) { try { Directory.CreateDirectory(Path.GetDirectoryName(d)!); File.WriteAllText(d, js); Console.WriteLine($"Wrote {d}"); } catch (Exception ex) { Console.WriteLine($"Skip {d}: {ex.Message}"); } }

    var catCounts = picked.GroupBy(p => p.cat).OrderByDescending(g => g.Count());
    Console.WriteLine($"\nCatalog: {picked.Count} items (floor>={floor} caps, max {maxItems}, from {byName.Count} unique named items).");
    foreach (var g in catCounts) Console.WriteLine($"  {g.Key,-14} {g.Count()}");
    return;
}

// perkdump <pluginPath> : list perks with "Add Activate Choice" entry points (label + priority).
if (cmd == "perkdump")
{
    var ppath = args[1];
    var pm = Fallout4Mod.CreateFromBinaryOverlay(ModPath.FromPath(ppath), Fallout4Release.Fallout4);
    Console.WriteLine($"\n=== Perks in {Path.GetFileName(ppath)} ===");
    foreach (var pk in pm.Perks)
    {
        bool any = false;
        foreach (var eff in pk.Effects)
        {
            if (eff is not APerkEntryPointEffect ep) continue;
            var epName = ep.EntryPoint.ToString();
            if (!epName.Contains("Activate", StringComparison.OrdinalIgnoreCase)) continue;
            if (!any) { Console.WriteLine($"  PERK {pk.FormKey.ID:X6}  {pk.EditorID}"); any = true; }
            string label = "";
            if (ep is PerkEntryPointAddActivateChoice ac)
                label = $" button='{ac.ButtonLabel}'";
            Console.WriteLine($"    entry={epName} priority={ep.Priority}{label} conds={ep.Conditions.Count}");
            foreach (var pc in ep.Conditions)
                foreach (var c in pc.Conditions)
                {
                    var fn = (c.Data as IFunctionConditionDataGetter)?.Function.ToString() ?? c.Data?.GetType().Name;
                    var p1 = (c.Data as IFunctionConditionDataGetter)?.ParameterOneRecord.FormKey.ID.ToString("X6");
                    Console.WriteLine($"        cond: {fn} p1={p1} cmp={(c as IConditionFloatGetter)?.ComparisonValue} op={c.CompareOperator}");
                }
        }
    }
    Console.WriteLine("\nPerkdump done.");
    return;
}

// smocarryfix <out.esp> : override SMO's HC_ReduceCarryWeightAbility so it subtracts 0.
// Save- and MCM-independent: our base-30 GMST is the sole carry-weight authority.
if (cmd == "smocarryfix")
{
    var smoPath = @"D:\Games\ModlistDownloads\mods\Survival Mode Overhaul\Survival Mode Overhaul.esp";
    var fixOut = args.Length > 1 ? args[1]
        : @"D:\Games\ModlistDownloads\mods\Fallen World Hardcore\FWHardcore_SMO_CarryFix.esp";
    var smo = Fallout4Mod.CreateFromBinaryOverlay(ModPath.FromPath(smoPath), Fallout4Release.Fallout4);
    var fix = new Fallout4Mod(ModKey.FromNameAndExtension("FWHardcore_SMO_CarryFix.esp"), Fallout4Release.Fallout4);
    var spell = smo.Spells.First(s => s.EditorID == "HC_ReduceCarryWeightAbility");
    var ov = fix.Spells.GetOrAddAsOverride(spell);
    foreach (var ef in ov.Effects)
        if (ef.Data != null) ef.Data.Magnitude = 0f;
    Directory.CreateDirectory(Path.GetDirectoryName(fixOut)!);
    fix.WriteToBinary(fixOut, new Mutagen.Bethesda.Plugins.Binary.Parameters.BinaryWriteParameters
    {
        ModKey = Mutagen.Bethesda.Plugins.Binary.Parameters.ModKeyOption.CorrectToPath
    });
    Console.WriteLine($"Wrote {fixOut}  (zeroed {ov.Effects.Count} effect magnitudes on HC_ReduceCarryWeightAbility)");
    return;
}

// gdump <pluginPath> <substr> : dump globals + magic-effect AV magnitudes from ANY plugin.
if (cmd == "gdump")
{
    var gpath = args[1];
    var sub  = args.Length > 2 ? args[2] : "carry";
    var m = Fallout4Mod.CreateFromBinaryOverlay(ModPath.FromPath(gpath), Fallout4Release.Fallout4);
    Console.WriteLine($"\n=== Globals in {Path.GetFileName(gpath)} matching '{sub}' ===");
    foreach (var gl in m.Globals)
    {
        if (gl.EditorID == null || !gl.EditorID.Contains(sub, StringComparison.OrdinalIgnoreCase)) continue;
        var val = gl switch
        {
            IGlobalFloatGetter f => f.Data?.ToString() ?? "null",
            IGlobalIntGetter i   => i.Data?.ToString() ?? "null",
            IGlobalShortGetter s => s.Data?.ToString() ?? "null",
            _ => "?"
        };
        Console.WriteLine($"  {gl.FormKey.ID:X6}  {gl.EditorID,-40} = {val}");
    }
    Console.WriteLine($"\n=== Spells whose EditorID contains '{sub}' (effects + conditions) ===");
    foreach (var sp in m.Spells)
    {
        if (sp.EditorID == null || !sp.EditorID.Contains(sub, StringComparison.OrdinalIgnoreCase)) continue;
        Console.WriteLine($"  SPELL {sp.FormKey.ID:X6} {sp.EditorID}  type={sp.Type}");
        foreach (var ef in sp.Effects)
        {
            Console.WriteLine($"    effect mgef={ef.BaseEffect.FormKey.ID:X6} mag={ef.Data?.Magnitude} area={ef.Data?.Area} dur={ef.Data?.Duration}");
            foreach (var c in ef.Conditions)
            {
                var fn = (c.Data as IFunctionConditionDataGetter)?.Function.ToString() ?? c.Data?.GetType().Name;
                var p1 = (c.Data as IFunctionConditionDataGetter)?.ParameterOneRecord.FormKey.ID.ToString("X6");
                Console.WriteLine($"      cond: {fn} p1={p1} cmp={(c as IConditionFloatGetter)?.ComparisonValue} op={c.CompareOperator} runOn={(c.Data as IFunctionConditionDataGetter)?.RunOnType}");
            }
        }
    }
    Console.WriteLine($"\n=== Globals 420-440 range (toggles) ===");
    foreach (var gl in m.Globals)
    {
        var id = gl.FormKey.ID;
        if (id < 0x420 || id > 0x440) continue;
        var val = gl switch { IGlobalFloatGetter f => f.Data?.ToString(), IGlobalIntGetter i => i.Data?.ToString(), IGlobalShortGetter s => s.Data?.ToString(), _ => "?" };
        Console.WriteLine($"  {id:X6}  {gl.EditorID,-40} = {val}");
    }
    Console.WriteLine("\nGdump done.");
    return;
}

// glob <pluginPath> <substr> : print global variables whose EditorID contains substr.
if (cmd == "glob")
{
    var sub = args.Length > 2 ? args[2] : "weightlifting";
    Console.WriteLine($"\n=== Globals matching '{sub}' ===");
    foreach (var gl in esm.Globals)
    {
        if (gl.EditorID == null || !gl.EditorID.Contains(sub, StringComparison.OrdinalIgnoreCase)) continue;
        var val = gl switch
        {
            IGlobalFloatGetter f => f.Data?.ToString() ?? "null",
            IGlobalIntGetter i   => i.Data?.ToString() ?? "null",
            IGlobalShortGetter s => s.Data?.ToString() ?? "null",
            _ => "?"
        };
        Console.WriteLine($"  {gl.FormKey.ID:X6}  {gl.EditorID,-36} = {val}");
    }
    Console.WriteLine("\nGlob done.");
    return;
}

// Phase 1b: resolve the real load-order winner for the carry-weight GMSTs, exactly as the
// game would. Builds the mini load order (Fallout4.esm -> Anomaly -> Difficulty -> CarryWeight)
// and reports the winning value AND which plugin supplies it.
if (cmd == "resolve")
{
    var mods = new (string Name, string Path)[]
    {
        ("Fallout4.esm",                   @"D:\Games\ModlistDownloads\Stock Folder\Data\Fallout4.esm"),
        ("Fallout Anomaly Overwrites.esp", @"D:\Games\ModlistDownloads\mods\Fallout Anomaly Master Files\Fallout Anomaly Overwrites.esp"),
        ("FWHardcore_Difficulty.esp",      @"D:\Games\ModlistDownloads\mods\Fallen World Hardcore\FWHardcore_Difficulty.esp"),
        ("FWHardcore_CarryWeight.esp",     @"D:\Games\ModlistDownloads\mods\Fallen World Hardcore\FWHardcore_CarryWeight.esp"),
    };

    var overlays = new List<(string Name, IFallout4ModGetter Mod)>();
    foreach (var m in mods)
        overlays.Add((m.Name, Fallout4Mod.CreateFromBinaryOverlay(ModPath.FromPath(m.Path), Fallout4Release.Fallout4)));

    // Game conflict rule: highest-priority (latest-loaded) plugin that contains the record wins.
    var priorityHighToLow = Enumerable.Reverse(overlays).ToList();

    Console.WriteLine("\n=== Winning override for carry-weight GMSTs (game's view) ===");
    foreach (var eid in new[] { "fAVDCarryWeightBase", "fAVDCarryWeightMult" })
    {
        var hits = overlays
            .Where(o => o.Mod.GameSettings.Any(g => g.EditorID == eid))
            .Select(o => o.Name).ToList();

        var winner = priorityHighToLow
            .FirstOrDefault(o => o.Mod.GameSettings.Any(g => g.EditorID == eid));
        var rec = winner.Mod?.GameSettings.FirstOrDefault(g => g.EditorID == eid);
        var val = rec is IGameSettingFloatGetter f ? f.Data?.ToString() ?? "null" : "(none)";

        Console.WriteLine($"  {eid,-24} = {val,-6}  WINNER: {winner.Name ?? "(none)"}");
        Console.WriteLine($"      set by: {string.Join(", ", hits)}");
    }
    Console.WriteLine("\nResolve done.");
    return;
}

// Phase 2: author a tiny override plugin. We set the carry-weight game settings
// to: base = 50, per-Strength multiplier = 5. EditorIDs/values are confirmed by
// the dump above; adjust the targets list if the dump shows different names.
var outPath = args.Length > 2 ? args[2]
    : @"D:\Projects\FO4RecordEditor\Tools\CarryWeightPatch\out\FWHardcore_CarryWeight.esp";

// target EditorID -> desired float value
// Tuned so a natural character (STR + S7 Weightlifting, no perks/chems/gear) caps at 100:
//   base 30 + STR*2 (max +20 @ STR10) + Weightlifting up to +50 @ skill100 = 100 ceiling.
var targets = new Dictionary<string, float>(StringComparer.OrdinalIgnoreCase)
{
    ["fAVDCarryWeightBase"] = 30f,
    ["fAVDCarryWeightMult"] = 2f,
};

var patch = new Fallout4Mod(ModKey.FromNameAndExtension("FWHardcore_CarryWeight.esp"), Fallout4Release.Fallout4);

int copied = 0;
foreach (var g in esm.GameSettings)
{
    if (g.EditorID == null || !targets.TryGetValue(g.EditorID, out var want)) continue;
    if (g is not IGameSettingFloatGetter) { Console.WriteLine($"  SKIP {g.EditorID}: not a float GMST"); continue; }
    var ov = patch.GameSettings.GetOrAddAsOverride(g);
    ((GameSettingFloat)ov).Data = want;
    Console.WriteLine($"  override {g.EditorID} = {want}");
    copied++;
}

if (copied == 0)
{
    Console.WriteLine("\nNo target GMSTs found in master — nothing written. Check EditorIDs against the dump.");
    return;
}

Directory.CreateDirectory(Path.GetDirectoryName(outPath)!);
patch.WriteToBinary(outPath);
Console.WriteLine($"\nWrote {copied} override(s) -> {outPath}");
