using System;
using System.IO;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class WritebackTests : IClassFixture<WorldFixture>
{
    private readonly WorldFixture _world;

    public WritebackTests(WorldFixture world) => _world = world;

    private static FormKey M(uint id) => new("Master.esm", id);

    [Fact]
    public void OverridesEditsSavesAndReloads()
    {
        using var temp = new TempDirectory();
        var output = Path.Combine(temp.Path, "Edits.esp");
        using (var env = RadicleEnvironment.Load(new[] { _world.Master }))
        using (var plugin = PluginFile.Create("Edits.esp"))
        {
            var refr = env.Winner(M(0x803))!.Value;
            Assert.Equal(PlacedCopyStatus.Inserted, plugin.CopyPlacedOverride(env, refr));
            Assert.Equal(PlacedCopyStatus.AlreadyExists, plugin.CopyPlacedOverride(env, refr));
            Assert.Equal(PlacedCopyStatus.Replaced, plugin.CopyPlacedOverride(env, refr, overwrite: true));
            var created = plugin.CopyPlacedNew(env, refr);
            Assert.Equal(new FormKey("Edits.esp", 0x800), created);

            Assert.True(plugin.SetTransform(M(0x803), new Vector3(1, 2, 3), new Vector3(0, 0, 0.25f)));
            Assert.True(plugin.SetScale(M(0x803), null));
            Assert.True(plugin.SetEditorId(M(0x803), "Moved"));
            Assert.True(plugin.SetRecordFlags(M(0x803), 0x800));
            Assert.True(plugin.SetComments(M(0x803), null));
            Assert.True(plugin.SetLocationReference(env, M(0x803), M(0x831)));
            Assert.True(plugin.SetOwnership(env, M(0x803), M(0x830), noCrime: true, factionRank: 2));
            Assert.True(plugin.SetEnableParent(env, M(0x803), null, 0));
            Assert.True(plugin.SetLinkedReferences(env, M(0x803), new[] { new LinkedReference(null, M(0x804)), new LinkedReference(M(0x830), M(0x821)) }));
            Assert.True(plugin.SetActivateParentDelay(M(0x803), 0, 4.0f));
            Assert.False(plugin.SetActivateParentDelay(M(0x803), 3, 4.0f));
            Assert.False(plugin.SetTeleport(env, M(0x803), M(0x804), new Vector3(0, 0, 0), new Vector3(0, 0, 0)));
            Assert.False(plugin.SetPrimitive(M(0x803), new Vector3(1, 1, 1), 1));
            Assert.False(plugin.SetComments(M(0x999), "x"));
            var error = Assert.Throws<RadicleException>(() => plugin.SetOwnership(env, M(0x803), new FormKey("Nope.esm", 1), false, null));
            Assert.Equal(Status.InvalidArgument, error.Status);
            plugin.SortMasters(env);
            plugin.Write(output);
            Assert.Equal(File.ReadAllBytes(output), plugin.ToBytes());
        }

        using var reloaded = RadicleEnvironment.Load(new[] { _world.Master, output });
        var winner = reloaded.Winner(M(0x803))!.Value;
        var info = reloaded.Info(winner);
        Assert.Equal(1, info.PluginIndex);
        Assert.Equal("Moved", info.EditorId);
        Assert.Equal(0x800u, info.RecordFlags);
        var placed = reloaded.PlacedReference(winner);
        Assert.Equal(new Vector3(1, 2, 3), placed.Position);
        Assert.Null(placed.Scale);
        Assert.Null(placed.Comments);
        Assert.Equal(M(0x831), placed.LocationReference);
        Assert.Equal(M(0x830), placed.Owner);
        Assert.True(placed.NoCrime);
        Assert.Equal(2, placed.FactionRank);
        Assert.Null(placed.EnableParent);
        Assert.Equal(new[] { M(0x804), M(0x821) }, placed.LinkedReferences.Select(l => l.Reference!.Value));
        Assert.Equal(4.0f, placed.ActivateParents.Single().Delay);
        Assert.Contains(reloaded.CellChildren(M(0x802)), c => reloaded.Info(c.Record).FormKey == new FormKey("Edits.esp", 0x800));

        using var reread = PluginFile.Read(output);
        Assert.Equal(File.ReadAllBytes(output), reread.ToBytes());
    }

    [Fact]
    public void MovesAPlacedReferenceWithinItsCellAndRefusesAMissingTargetCell()
    {
        using var env = RadicleEnvironment.Load(new[] { _world.Master });
        using var plugin = PluginFile.Create("Moves.esp");
        Assert.Equal(PlacedCopyStatus.Inserted, plugin.CopyPlacedOverride(env, env.Winner(M(0x803))!.Value));

        Assert.Equal(PlacedMoveStatus.Moved, plugin.MoveTransform(M(0x803), new Vector3(5000, 9000, 30), new Vector3(0, 0, 0)));
        var error = Assert.Throws<RadicleException>(() => plugin.MoveTransform(M(0x803), new Vector3(0, 0, 0), new Vector3(0, 0, 0)));
        Assert.Equal(Status.NotFound, error.Status);
    }

    private static byte[] Ctda(RadicleCondition c) =>
        new[] { c.FlagsByte, (byte)0, (byte)0, (byte)0 }.Concat(U32(c.ComparisonRaw)).Concat(BitConverter.GetBytes(c.Function))
            .Concat(BitConverter.GetBytes(c.Unknown2)).Concat(BitConverter.GetBytes(c.Param1)).Concat(BitConverter.GetBytes(c.Param2))
            .Concat(U32(c.RunOn, c.Reference)).Concat(BitConverter.GetBytes(c.Unknown3)).ToArray();

    [Fact]
    public void ReadsAndReplacesARecordConditionList()
    {
        using var temp = new TempDirectory();
        var first = new RadicleCondition(0x20, 0x3F800000u, 1, 0, 0x800, 0, 0, 0, -1, "Hello", null);
        var second = new RadicleCondition(0x01, 0u, 0x10, 0, -1, 3, 0, 0x800, -1, null, null);
        var source = temp.Write("Conditions.esp", Build(0, Array.Empty<string>(),
            Top("PERK", new Rec("PERK", 0x800, new[] { ("EDID", Z("Perk")), ("CTDA", Ctda(first)), ("CIS1", Z("Hello")), ("CTDA", Ctda(second)) }))));
        var key = new FormKey("Conditions.esp", 0x800);
        using (var env = RadicleEnvironment.Load(new[] { source }))
        {
            Assert.Equal<RadicleCondition>(new[] { first, second }, env.Conditions(env.Winner(key)!.Value));
        }

        using (var plugin = PluginFile.Read(source))
        {
            Assert.Equal(ConditionWriteStatus.Unchanged, plugin.SetConditions(key, null, new[] { first, second }));
            Assert.Equal(ConditionWriteStatus.Updated, plugin.SetConditions(key, null, new[] { second, first }));
            Assert.Equal(ConditionWriteStatus.Unchanged, plugin.SetConditions(key, null, new[] { second, first }));
            Directory.CreateDirectory(Path.Combine(temp.Path, "out"));
            var output = Path.Combine(temp.Path, "out", "Conditions.esp");
            plugin.Write(output);
            using var reloaded = RadicleEnvironment.Load(new[] { output });
            Assert.Equal<RadicleCondition>(new[] { second, first }, reloaded.Conditions(reloaded.Winner(key)!.Value));
        }
    }

    // Constraint: Regression: the engine typed condition parameters itself, so a float parameter was read as an int, an alias-switched slot as a FormID, a master-indexed id against the wrong plugin, and one bad FormID failed the whole list.
    [Fact]
    public void ReadsTypedConditionParameters()
    {
        using var temp = new TempDirectory();
        var stageDone = new RadicleCondition(0x04, 0x801u, 59, 0, 0x801, 10, 2, 0x01000802u, -1, null, null);
        var withinDistance = new RadicleCondition(0x00, 0u, 639, 0, 0x01000802, BitConverter.SingleToInt32Bits(5.5f), 0, 0, -1, null, null);
        var aliasSwitched = new RadicleCondition(0x02, 0u, 639, 0, 3, 0, 0, 0, -1, null, null);
        var badRace = new RadicleCondition(0x00, 0u, 69, 0, 0x07000800, 0, 0, 0, -1, null, null);
        var unknown = new RadicleCondition(0x00, 0u, 3, 0, 7, 9, 0, 0, -1, null, null);
        var variable = new RadicleCondition(0x00, 0u, 629, 0, 0x801, 0, 0, 0, -1, null, "MyVar");
        var source = temp.Write("Typed.esp", Build(0, new[] { "Master.esm" },
            Top("PERK", new Rec("PERK", 0x01000800, new[]
            {
                ("EDID", Z("Typed")), ("CTDA", Ctda(stageDone)), ("CTDA", Ctda(withinDistance)), ("CTDA", Ctda(aliasSwitched)),
                ("CTDA", Ctda(badRace)), ("CTDA", Ctda(unknown)), ("CTDA", Ctda(variable)), ("CIS2", Z("MyVar")),
            }))));
        using var env = RadicleEnvironment.Load(new[] { _world.Master, source });
        var record = env.Winner(new FormKey("Typed.esp", 0x800))!.Value;
        var typed = env.ConditionParameters(record, env.Conditions(record));

        Assert.Equal(6, typed.Count);
        Assert.Equal("GetStageDone", typed[0].FunctionName);
        Assert.Equal(new RadicleConditionParam(ConditionParamKind.Form, "quest", 0x801, 0, null, M(0x801), false), typed[0].Param1);
        Assert.Equal(new RadicleConditionParam(ConditionParamKind.Other, "quest_stage", 10, 10, null, null, false), typed[0].Param2);
        Assert.Equal(new RadicleConditionParam(ConditionParamKind.Form, "global", 0x801, 0, null, M(0x801), false), typed[0].Comparison);
        Assert.Equal(new RadicleConditionParam(ConditionParamKind.Form, "reference", 0x01000802, 0, null, new FormKey("Typed.esp", 0x802), false), typed[0].Reference);

        Assert.Equal(new FormKey("Typed.esp", 0x802), typed[1].Param1.Form);
        Assert.Equal(ConditionParamKind.Float, typed[1].Param2.Kind);
        Assert.Equal(5.5, typed[1].Param2.Number);
        Assert.Equal(ConditionParamKind.Float, typed[1].Comparison.Kind);

        Assert.Equal(new RadicleConditionParam(ConditionParamKind.Index, "reference", 3, 3, null, null, false), typed[2].Param1);

        Assert.Equal(ConditionParamKind.Form, typed[3].Param1.Kind);
        Assert.Null(typed[3].Param1.Form);
        Assert.True(typed[3].Param1.FormUnresolved);

        Assert.Null(typed[4].FunctionName);
        Assert.Equal(ConditionParamKind.None, typed[4].Param1.Kind);
        Assert.Equal(7, typed[4].Param1.Raw);

        Assert.Equal(M(0x801), typed[5].Param1.Form);
        Assert.Equal(new RadicleConditionParam(ConditionParamKind.String, "string", 0, 0, "MyVar", null, false), typed[5].Param2);
    }

    private static bool Has(byte[] bytes, string text) => bytes.AsSpan().IndexOf(System.Text.Encoding.ASCII.GetBytes(text)) >= 0;

    [Fact]
    public void HeaderSettersSurviveWriteAndReload()
    {
        using var temp = new TempDirectory();
        var output = Path.Combine(temp.Path, "Header.esp");
        using (var plugin = PluginFile.Create("Header.esp"))
        {
            plugin.SetAuthor("Nomad");
            plugin.SetDescription("Header description");
            plugin.SetMasterFlag(true);
            plugin.SetLocalizedFlag(true);
            Assert.True(Has(plugin.ToBytes(), "Nomad"));
            Assert.True(Has(plugin.ToBytes(), "Header description"));
            plugin.Write(output);
        }

        using var env = RadicleEnvironment.Load(new[] { output });
        var info = Assert.Single(env.Plugins);
        Assert.True(info.MasterFlag);
        Assert.True(info.Localized);
    }

    private static byte[] RemapSource() => Build(0, new[] { "Fallout4.esm", "Drop.esm", "Keep.esp" },
        Top("KYWD", new Rec("KYWD", 0x03000800, new[] { ("EDID", Z("Local")) }),
            new Rec("KYWD", 0x0200009A, new[] { ("EDID", Z("KeepRecord")) })));

    [Fact]
    public void RemapMastersDropsAnUnusedMasterAndRefusesAUsedOneWithoutChange()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Patch.esp", RemapSource());
        using var plugin = PluginFile.Read(path);
        var before = plugin.CollectFormKeys().OrderBy(k => k, StringComparer.Ordinal).ToArray();
        Assert.Contains("000800:Patch.esp", before);
        Assert.Contains("00009A:Keep.esp", before);

        var refused = Assert.Throws<RadicleException>(() =>
            plugin.RemapMasters(new[] { new PluginMaster("Fallout4.esm") }));
        Assert.Contains("destination master table", refused.Message);
        Assert.Equal(before, plugin.CollectFormKeys().OrderBy(k => k, StringComparer.Ordinal).ToArray());

        plugin.RemapMasters(new[] { new PluginMaster("Fallout4.esm"), new PluginMaster("Keep.esp") });
        var output = Path.Combine(temp.Path, "out", "Patch.esp");
        Directory.CreateDirectory(Path.GetDirectoryName(output)!);
        plugin.Write(output);
        using var reloaded = PluginFile.Read(output);
        var after = reloaded.CollectFormKeys();
        Assert.Contains("000800:Patch.esp", after);
        Assert.Contains("00009A:Keep.esp", after);
        Assert.False(Has(reloaded.ToBytes(), "Drop.esm"));
    }

    [Fact]
    public void CopiesARecordAsAnOverrideAndAsANewRecord()
    {
        using var source = PluginFile.Create("Source.esp");
        using var target = PluginFile.Create("Target.esp");
        var key = source.CreateRecord("ARMO", "NewArmor");

        Assert.Equal(RecordCopyStatus.Inserted, target.CopyRecordOverride(source, key));
        Assert.Equal(RecordCopyStatus.AlreadyExists, target.CopyRecordOverride(source, key));
        Assert.Equal(RecordCopyStatus.Replaced, target.CopyRecordOverride(source, key, overwrite: true));
        var missing = Assert.Throws<RadicleException>(() => target.CopyRecordOverride(source, new FormKey(key.Plugin, 0x8FF)));
        Assert.Equal(Status.NotFound, missing.Status);

        var created = target.CopyRecordAsNew(source, key, editorId: "ArmorCopy");
        Assert.Equal("Target.esp", created.Plugin);
        Assert.True(Has(target.ToBytes(), "ArmorCopy"));
    }

    [Fact]
    public void FactionEyesAndComponentWritersEditTheRecords()
    {
        using var temp = new TempDirectory();
        using var plugin = PluginFile.Read(temp.Write("Authoring.esp", Build(0, Array.Empty<string>(),
            Top("MISC", new Rec("MISC", 0x800, new[] { ("EDID", Z("Item")), ("CVPA", U32(0x804, 1)) })),
            Top("ARMO", new Rec("ARMO", 0x804, new[] { ("EDID", Z("Plain")) })))));
        var faction = plugin.CreateFaction("TestFaction", "Test Faction");
        Assert.True(plugin.AddFactionRelation(faction, faction, 5, 1));
        Assert.True(plugin.SetFactionRelation(faction, 0, faction, 7, 2));
        Assert.True(plugin.AddFactionRank(faction, 0, new FactionRankDraft(new DialogueText(NameKind.Inline, "Recruit", 0), null)));
        Assert.True(plugin.SetFactionRank(faction, 0, 1, new FactionRankDraft(new DialogueText(NameKind.Inline, "Captain", 0), null)));
        Assert.True(Has(plugin.ToBytes(), "Captain"));
        Assert.False(Has(plugin.ToBytes(), "Recruit"));
        Assert.True(plugin.RemoveFactionRank(faction, 0));
        Assert.False(Has(plugin.ToBytes(), "Captain"));
        Assert.True(plugin.RemoveFactionRelation(faction, 0));
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.RemoveFactionRelation(faction, 5)).Status);
        Assert.False(plugin.RemoveFactionRelation(new FormKey("Authoring.esp", 0x8FF), 0));

        var item = new FormKey("Authoring.esp", 0x800);
        var plain = new FormKey("Authoring.esp", 0x804);
        Assert.True(plugin.SetComponents(item, new[] { new RecordComponent(plain, 7) }));
        Assert.True(plugin.ToBytes().AsSpan().IndexOf(U32(0x804, 7)) >= 0);
        Assert.False(plugin.SetComponents(new FormKey("Authoring.esp", 0x999), new[] { new RecordComponent(plain, 7) }));
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() =>
            plugin.SetComponents(plain, new[] { new RecordComponent(plain, 7) })).Status);
    }
}
