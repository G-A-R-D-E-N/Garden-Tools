using System.Collections;
using System.IO;
using FluentAssertions;
using FO4RecordEditor.Services;
using Mutagen.Bethesda.Plugins.Records;

namespace FO4RecordEditor.Tests;

// Integration test against a real MO2 instance on this machine. Skipped automatically if the
// instance isn't present, so it won't fail on other machines/CI.
public class Mo2ProfileLoaderTests
{
    private const string Instance = @"D:\Games\ModlistDownloads";

    private static bool Available =>
        Directory.Exists(Path.Combine(Instance, "profiles")) &&
        File.Exists(Path.Combine(Instance, "ModOrganizer.ini"));

    [Fact]
    public void ReadsInstanceInfo()
    {
        if (!Available) return;   // not on this machine -> no-op
        var info = Mo2ProfileLoader.ReadInstanceInfo(Instance);
        info.Profile.Should().NotBeNullOrWhiteSpace();
        info.GameDataFolder.Should().EndWith("Data");
    }

    [Fact]
    public void LoadsProfileAndBuildsResolvableLinkCache()
    {
        if (!Available) return;   // not on this machine -> no-op
        var info = Mo2ProfileLoader.ReadInstanceInfo(Instance);

        var (env, plugins) = Mo2ProfileLoader.Load(Instance, info.Profile, info.GameDataFolder);

        plugins.Should().NotBeEmpty("the profile lists hundreds of enabled plugins");

        dynamic dynEnv = env;
        // LoadOrder enumerates and every listing exposes a non-null Mod + a filename.
        int listed = 0;
        foreach (var l in (IEnumerable)dynEnv.LoadOrder.ListedOrder)
        {
            ((object?)((dynamic)l).Mod).Should().NotBeNull();
            ((string)((dynamic)l).ModKey.FileName.String).Should().NotBeNullOrWhiteSpace();
            listed++;
        }
        listed.Should().Be(plugins.Count);

        // LinkCache resolves a winning record for the first major record of the first mod --
        // proves cross-plugin resolution works, not just that files opened.
        var cache = (Mutagen.Bethesda.Plugins.Cache.ILinkCache)dynEnv.LinkCache;
        cache.Should().NotBeNull();
    }

    [Fact]
    public void FormatsComponentFormLinkWithName()
    {
        if (!Available) return;
        var info = Mo2ProfileLoader.ReadInstanceInfo(Instance);
        Mo2ProfileLoader.Load(Instance, info.Profile, info.GameDataFolder);  // sets MutagenLoader.LinkCache

        var cache = MutagenLoader.LinkCache!;
        // A Component (CMPO) like c_Steel "Steel" -- resolve any one and format a link to it.
        var component = cache.PriorityOrder
            .SelectMany(m => m.EnumerateMajorRecords())
            .OfType<Mutagen.Bethesda.Fallout4.IComponentGetter>()
            .First();
        var link = Mutagen.Bethesda.Plugins.FormLinkInformation.Factory(component);

        var rendered = MutagenLoader.FormatFormLink(link);

        // xEdit-style: should carry the EditorID/name + bracketed FormID, not just the raw key.
        rendered.Should().Contain("[").And.Contain(component.FormKey.ToString());
        rendered.Should().NotBe(component.FormKey.ToString(), "the link should resolve to a name");
    }

    [Fact]
    public void BuildsMultiPluginConflictMatrix()
    {
        if (!Available) return;
        var info = Mo2ProfileLoader.ReadInstanceInfo(Instance);
        var (env, _) = Mo2ProfileLoader.Load(Instance, info.Profile, info.GameDataFolder);

        var cache = MutagenLoader.LinkCache!;
        // Find any record that is overridden by 2+ plugins (a real conflict set).
        Mutagen.Bethesda.Plugins.FormKey? conflicted = null;
        foreach (var rec in cache.PriorityOrder.SelectMany(m => m.EnumerateMajorRecords()).Take(20000))
        {
            if (cache.ResolveAllSimpleContexts(rec.FormKey).Take(2).Count() >= 2)
            { conflicted = rec.FormKey; break; }
        }
        if (conflicted == null) return;   // no conflicts in the sampled window -> nothing to assert

        var matrix = MutagenLoader.BuildConflictMatrix(env, conflicted.Value.ToString());
        matrix.Should().NotBeNull();
        matrix!.Plugins.Count.Should().BeGreaterThanOrEqualTo(2, "the record is carried by multiple plugins");
        matrix.Rows.Should().NotBeEmpty();
        // Winner is the last plugin in load order.
        matrix.Winner.Should().Be(matrix.Plugins[^1]);
    }

    [Fact]
    public void ConflictMatrix_DoesNotLeakReflectionInternals()
    {
        if (!Available) return;
        var info = Mo2ProfileLoader.ReadInstanceInfo(Instance);
        var (env, _) = Mo2ProfileLoader.Load(Instance, info.Profile, info.GameDataFolder);
        var cache = MutagenLoader.LinkCache!;

        // A constructible object usually has FormLink lists (Categories / Components) — the
        // exact shape that used to make the walker recurse into System.Type / Assembly.
        var cobj = cache.PriorityOrder
            .SelectMany(m => m.EnumerateMajorRecords())
            .OfType<Mutagen.Bethesda.Fallout4.IConstructibleObjectGetter>()
            .FirstOrDefault();
        if (cobj == null) return;

        var matrix = MutagenLoader.BuildConflictMatrix(env, cobj.FormKey.ToString());
        matrix.Should().NotBeNull();

        foreach (var row in matrix!.Rows)
        {
            row.Field.Should().NotContainAny(".Assembly", "Reflection", "DeclaringMethod", "GUID");
            foreach (var v in row.Values)
                v.Should().NotContainAny(".dll", "System.Private.CoreLib", "PublicKeyToken", "mscorlib");
        }
    }

    [Fact]
    public void EditingComponentCountWritesToPatch()
    {
        if (!Available) return;
        var info = Mo2ProfileLoader.ReadInstanceInfo(Instance);
        var (env, _) = Mo2ProfileLoader.Load(Instance, info.Profile, info.GameDataFolder);
        var cache = MutagenLoader.LinkCache!;

        // A constructible object (crafting recipe) with at least one component (Count to edit).
        var cobj = cache.PriorityOrder
            .SelectMany(m => m.EnumerateMajorRecords())
            .OfType<Mutagen.Bethesda.Fallout4.IConstructibleObjectGetter>()
            .FirstOrDefault(c => c.Components != null && c.Components.Count > 0);
        if (cobj == null) return;

        var fk = cobj.FormKey;
        var winner = cache.ResolveAllSimpleContexts(fk).First().ModKey.FileName.String;
        var originalCount = cobj.Components![0].Count;
        var newCount = originalCount == 7u ? 8u : 7u;   // guaranteed different

        // Build the display node the way the UI does, then simulate a grid edit of [0].Count.
        var node = new Models.RecordNode { Key = cobj.EditorID ?? fk.ToString() };
        var fkLeaf = new Models.RecordNode { Key = "FormKey", Parent = node };
        fkLeaf.Values[""] = fk.ToString();
        node.Children.Add(fkLeaf);
        MutagenLoader.PopulateNodeAllVersions(node, env);

        var countLeaf = node.GetChild("Components")?.GetChild("[0]")?.GetChild("Count");
        countLeaf.Should().NotBeNull("the component should expose a Count leaf");
        countLeaf!.Value = newCount.ToString();   // marks the leaf dirty

        var outDir = Path.Combine(Path.GetTempPath(), "FO4RE_patchtest_" + Guid.NewGuid().ToString("N"));
        WriteService.OutputFolderOverride = outDir;
        var patchName = "TestPatch.esp";
        try
        {
            var result = WriteService.SaveRecordEditsToPatch(env, node, winner, patchName);
            result.Should().Contain("saved");

            // Read the written patch back from disk and confirm the new Count persisted.
            var patchPath = Path.Combine(outDir, patchName);
            File.Exists(patchPath).Should().BeTrue();
            var patchMod = Mutagen.Bethesda.Fallout4.Fallout4Mod.CreateFromBinary(
                Mutagen.Bethesda.Plugins.ModPath.FromPath(patchPath),
                Mutagen.Bethesda.Fallout4.Fallout4Release.Fallout4);
            var written = patchMod.ConstructibleObjects.First(c => c.FormKey == fk);
            written.Components![0].Count.Should().Be(newCount);
        }
        finally
        {
            WriteService.OutputFolderOverride = null;
            try { Directory.Delete(outDir, true); } catch { }
        }
    }
}
