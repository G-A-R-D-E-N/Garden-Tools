using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class PerkEffectTests
{
    private const string Plugin = "Perks.esp";
    private static readonly string[] Masters = { "Master.esm" };

    private static Radicle.Interop.FormKey K(uint id) => new(Plugin, id);

    private static readonly Radicle.Interop.FormKey Quest = K(0x810);
    private static readonly Radicle.Interop.FormKey Ability = K(0x820);
    private static readonly Radicle.Interop.FormKey Perk = K(0x830);

    private static byte[] U8(params byte[] values) => values;

    private static byte[] U16(params ushort[] values) => values.SelectMany(BitConverter.GetBytes).ToArray();

    private static byte[] HeaderData() => new byte[] { 0, 0, 1, 1, 0 };

    private static byte[] Ctda(ushort function)
    {
        var bytes = new byte[32];
        BitConverter.GetBytes(function).CopyTo(bytes, 8);
        BitConverter.GetBytes(-1).CopyTo(bytes, 28);
        return bytes;
    }

    private static byte[] Fixture() =>
        Build(0, Masters,
            Top("QUST", new Rec("QUST", 0x01000810, new[] { ("EDID", Z("RadicleQuest")) })),
            Top("SPEL", new Rec("SPEL", 0x01000820, new[] { ("EDID", Z("RadicleAbility")) })),
            Top("PERK", new Rec("PERK", 0x01000830, new[]
            {
                ("EDID", Z("RadiclePerk")),
                ("DATA", HeaderData()),
                ("PRKE", U8(0, 1, 0)),
                ("DATA", U32(0x01000810).Concat(U16(42)).ToArray()),
                ("PRKF", Array.Empty<byte>()),
                ("PRKE", U8(2, 1, 3)),
                ("DATA", U8(0x26, 1, 1)),
                ("PRKC", U8(0)),
                ("CTDA", Ctda(100)),
                ("EPFT", U8(1)),
                ("EPFD", F32(2.5f)),
                ("PRKF", Array.Empty<byte>()),
            })));

    private static PluginFile Open(TempDirectory temp) => PluginFile.Read(temp.Write(Plugin, Fixture()));

    [Fact]
    public void ReadsQuestAndEntryPointEffects()
    {
        using var temp = new TempDirectory();
        using var plugin = Open(temp);

        var effects = plugin.PerkEffects(Perk);
        Assert.Equal(2, effects.Count);

        Assert.Equal(PerkEffectKind.QuestStage, effects[0].Type);
        Assert.Equal(1, effects[0].Rank);
        Assert.Equal(Quest, effects[0].Quest);
        Assert.Equal(42, effects[0].QuestStage);

        Assert.Equal(PerkEffectKind.EntryPoint, effects[1].Type);
        Assert.Equal(0x26, effects[1].EntryPoint);
        Assert.Equal(1, effects[1].Function);
        Assert.Equal(1, effects[1].ConditionTabCount);
        var tab = Assert.Single(effects[1].Tabs);
        Assert.Equal(0, tab.RunOn);
        var condition = Assert.Single(tab.Conditions);
        Assert.Equal(100, condition.Function);
        Assert.Equal(-1, condition.Unknown3);
        Assert.Null(condition.Parameter1);
        Assert.Equal((byte?)1, effects[1].ParameterType);
        Assert.Equal(new byte[] { 0x00, 0x00, 0x20, 0x40 }, effects[1].Data);

        var error = Assert.Throws<RadicleException>(() => plugin.PerkEffects(Quest));
        Assert.Equal(Status.InvalidArgument, error.Status);
        error = Assert.Throws<RadicleException>(() => plugin.PerkEffects(K(0x999)));
        Assert.Equal(Status.NotFound, error.Status);
    }

    [Fact]
    public void WritesAddsAndRemovesEffectsAndConditions()
    {
        using var temp = new TempDirectory();
        using var plugin = Open(temp);

        var effects = plugin.PerkEffects(Perk).ToList();
        effects.Add(new PerkEffect
        {
            Type = PerkEffectKind.Ability,
            Rank = 2,
            Ability = Ability,
            Tabs = new[]
            {
                new PerkConditionTab
                {
                    RunOn = -1,
                    Conditions = new[]
                    {
                        new PerkCondition { Function = 77, Unknown3 = -1, Parameter1 = "Alias" },
                    },
                },
            },
        });

        Assert.True(plugin.SetPerkEffects(Perk, effects));
        Assert.False(plugin.SetPerkEffects(K(0x999), effects));
        var error = Assert.Throws<RadicleException>(() => plugin.SetPerkEffects(Quest, effects));
        Assert.Equal(Status.InvalidArgument, error.Status);
        error = Assert.Throws<RadicleException>(() => plugin.SetPerkEffects(Perk, new[]
        {
            new PerkEffect { Type = (PerkEffectKind)3 },
        }));
        Assert.Equal(Status.InvalidArgument, error.Status);

        var output = System.IO.Path.Combine(temp.Path, "saved", Plugin);
        System.IO.Directory.CreateDirectory(System.IO.Path.GetDirectoryName(output)!);
        plugin.Write(output);
        using var reloaded = PluginFile.Read(output);

        var read = reloaded.PerkEffects(Perk);
        Assert.Equal(3, read.Count);
        Assert.Equal(PerkEffectKind.Ability, read[2].Type);
        Assert.Equal(Ability, read[2].Ability);
        var tab = Assert.Single(read[2].Tabs);
        Assert.Equal(-1, tab.RunOn);
        var condition = Assert.Single(tab.Conditions);
        Assert.Equal(77, condition.Function);
        Assert.Equal("Alias", condition.Parameter1);

        Assert.True(reloaded.SetPerkEffects(Perk, Array.Empty<PerkEffect>()));
        Assert.Empty(reloaded.PerkEffects(Perk));
    }

// Constraint: Detects a regression where ConditionTabCount was copied from the caller instead of derived from Tabs.Count, which would let a mismatched caller drop the tab run.
    [Fact]
    public void DerivesConditionTabCountFromTabs()
    {
        using var temp = new TempDirectory();
        using var plugin = Open(temp);

        Assert.True(plugin.SetPerkEffects(Perk, new[]
        {
            new PerkEffect
            {
                Type = PerkEffectKind.EntryPoint,
                EntryPoint = 0x26,
                Function = 1,
                ConditionTabCount = 0,
                Tabs = new[]
                {
                    new PerkConditionTab
                    {
                        RunOn = -1,
                        Conditions = new[] { new PerkCondition { Function = 5, Unknown3 = -1 } },
                    },
                },
            },
        }));

        var output = System.IO.Path.Combine(temp.Path, "saved", Plugin);
        System.IO.Directory.CreateDirectory(System.IO.Path.GetDirectoryName(output)!);
        plugin.Write(output);
        using var reloaded = PluginFile.Read(output);
        var read = Assert.Single(reloaded.PerkEffects(Perk));
        Assert.Equal(1, read.ConditionTabCount);
        Assert.Single(read.Tabs);
    }

    [Fact]
    public void CreatesAPerkWithEffectsInOneCall()
    {
        using var temp = new TempDirectory();
        using var plugin = Open(temp);

        var created = plugin.CreatePerk("RadicleCreatedPerk", new[]
        {
            new PerkEffect
            {
                Type = PerkEffectKind.EntryPoint,
                EntryPoint = 0x3B,
                Function = 12,
                ParameterType = 2,
                EntryId = 0x0102,
                ButtonLabel = "Press",
                ScriptFlags = 1,
                Data = F32(1.5f),
            },
        });

        Assert.Equal(Plugin, created.Plugin);
        Assert.NotEqual(0u, created.LocalId);
        var read = Assert.Single(plugin.PerkEffects(created));
        Assert.Equal(PerkEffectKind.EntryPoint, read.Type);
        Assert.Equal(0x3B, read.EntryPoint);
        Assert.Equal("Press", read.ButtonLabel);
        Assert.Equal((ushort?)1, read.ScriptFlags);
        Assert.Equal(new byte[] { 0x00, 0x00, 0xC0, 0x3F }, read.Data);

        Assert.Throws<ArgumentNullException>(() => plugin.CreatePerk(null!));
        var error = Assert.Throws<RadicleException>(() => plugin.CreatePerk("RefusedPerk", new[]
        {
            new PerkEffect { Type = (PerkEffectKind)3 },
        }));
        Assert.Equal(Status.InvalidArgument, error.Status);
    }
}
