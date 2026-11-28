using System;
using System.IO;
using System.Linq;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class TypedPerkOmodTests
{
    private const string Plugin = "Base.esm";

    private static FormKey K(uint id) => new(Plugin, id);

    private static byte[] OmodRow(byte type, byte function, ushort property, uint value1, uint value2) =>
        new byte[] { type, 0, 0, 0, function, 0, 0, 0 }.Concat(BitConverter.GetBytes(property)).Concat(new byte[2])
            .Concat(U32(value1, value2)).Concat(F32(0f)).ToArray();

    private static byte[] OmodData(params byte[][] rows) =>
        U32(0, (uint)rows.Length).Concat(new byte[2]).Concat(U32(0)).Concat(new byte[2]).Concat(U32(0, 0, 0))
            .Concat(rows.SelectMany(row => row)).ToArray();

    private static (string, byte[])[] Effect(byte entryPoint, byte function, byte priority, params (string, byte[])[] parameters) =>
        new[] { ("PRKE", new byte[] { 2, 0, priority }), ("DATA", new byte[] { entryPoint, function, 0 }) }
            .Concat(parameters).Append(("PRKF", Array.Empty<byte>())).ToArray();

    private static string Fixture(TempDirectory temp) => temp.Write(Plugin, Build(1, Array.Empty<string>(),
        Top("KYWD", new Rec("KYWD", 0x802, new[] { ("EDID", Z("Form")) })),
        Top("PERK", new Rec("PERK", 0x830, new[] { ("EDID", Z("Typed")), ("DATA", new byte[] { 0, 2, 3, 1, 0 }) }
            .Concat(Effect(0x10, 3, 1, ("EPFT", new byte[] { 1 }), ("EPFD", F32(0.5f))))
            .Concat(Effect(0x11, 4, 2, ("EPFT", new byte[] { 2 }), ("EPFD", F32(1.5f, 2.5f))))
            .Concat(Effect(0x12, 10, 3, ("EPFT", new byte[] { 5 }), ("EPFD", U32(0x802))))
            .Concat(Effect(0x13, 13, 4, ("EPFT", new byte[] { 8 }), ("EPFD", U32(0x802).Concat(F32(3f)).ToArray())))
            .Concat(Effect(0x14, 5, 5, ("EPFT", new byte[] { 2 }), ("EPFD", U32(0x802).Concat(F32(4f)).ToArray())))
            .Concat(Effect(0x15, 1, 6, ("EPFT", new byte[] { 6 }), ("EPFD", Z("Label"))))
            .Concat(Effect(0x16, 1, 7))
            .Concat(Effect(0x17, 1, 8, ("EPFT", new byte[] { 9 }), ("EPFD", U32(0x802))))
            .ToArray())),
        Top("OMOD", new Rec("OMOD", 0x8E0, new[]
        {
            ("EDID", Z("Typed")), ("DATA", OmodData(
                OmodRow(0, 0, 10, 0xFFFFFFFEu, 0),
                OmodRow(1, 1, 66, BitConverter.SingleToUInt32Bits(-0.2f), 0),
                OmodRow(2, 0, 48, 1, 1),
                OmodRow(3, 0, 5, 7, 0),
                OmodRow(4, 2, 67, 0x802, 441),
                OmodRow(5, 0, 59, 4, 0),
                OmodRow(6, 2, 11, 0x802, BitConverter.SingleToUInt32Bits(5f)),
                OmodRow(9, 0, 12, 1, 2),
                OmodRow(4, 2, 68, 0, 7))),
        }))));

    [Fact]
    public void PerkEntryPointsExposeEveryTypedParameterShape()
    {
        using var temp = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { Fixture(temp) });
        var effects = env.PerkEntryPoints(K(0x830))!.Effects;
        Assert.Equal(8, effects.Count);

        var single = effects[0];
        Assert.Equal(PerkFunctionType.SingleFloat, single.FunctionType);
        Assert.Equal(0.5f, single.Value);
        Assert.Null(single.ValuePair);
        Assert.Null(single.Form);
        Assert.Null(single.ActorValue);
        Assert.Null(single.Text);

        var pair = effects[1];
        Assert.Equal(PerkFunctionType.FloatPair, pair.FunctionType);
        Assert.Null(pair.Value);
        Assert.Equal((1.5f, 2.5f), pair.ValuePair);

        var spell = effects[2];
        Assert.Equal(PerkFunctionType.Spell, spell.FunctionType);
        Assert.Equal(K(0x802), spell.Form);
        Assert.Null(spell.Value);
        Assert.Null(spell.ActorValue);

        var actorValue = effects[3];
        Assert.Equal(PerkFunctionType.ActorValue, actorValue.FunctionType);
        Assert.Equal(new PerkActorValue(K(0x802), 3f), actorValue.ActorValue);
        Assert.Null(actorValue.Form);
        Assert.Null(actorValue.ValuePair);

        var resolvedByFunction = effects[4];
        Assert.Equal((byte)2, resolvedByFunction.ParameterType);
        Assert.Equal(PerkFunctionType.ActorValue, resolvedByFunction.FunctionType);
        Assert.Equal(new PerkActorValue(K(0x802), 4f), resolvedByFunction.ActorValue);
        Assert.Null(resolvedByFunction.ValuePair);

        var text = effects[5];
        Assert.Equal(PerkFunctionType.Text, text.FunctionType);
        Assert.Equal("Label", text.Text);
        Assert.Null(text.Form);

        var none = effects[6];
        Assert.Equal(PerkFunctionType.None, none.FunctionType);
        Assert.Null(none.ParameterType);
        Assert.Null(none.Value);
        Assert.Null(none.ValuePair);
        Assert.Null(none.Form);
        Assert.Null(none.ActorValue);
        Assert.Null(none.Text);

        var unknown = effects[7];
        Assert.Equal((PerkFunctionType)9, unknown.FunctionType);
        Assert.Null(unknown.Value);
        Assert.Null(unknown.ValuePair);
        Assert.Null(unknown.Form);
        Assert.Null(unknown.ActorValue);
        Assert.Null(unknown.Text);
    }

    [Fact]
    public void OmodPropertiesExposeTypedValuesByValueType()
    {
        using var temp = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { Fixture(temp) });
        var rows = env.OmodProperties(K(0x8E0))!.Properties;
        Assert.Equal(9, rows.Count);

        Assert.Equal(OmodValueType.Int, rows[0].Kind);
        Assert.Equal(-2, rows[0].IntValue);
        Assert.Null(rows[0].FloatValue);
        Assert.Null(rows[0].BoolValue);
        Assert.Null(rows[0].EnumValue);
        Assert.Null(rows[0].Form);

        Assert.Equal(OmodValueType.Float, rows[1].Kind);
        Assert.Equal(-0.2f, rows[1].FloatValue);
        Assert.Null(rows[1].IntValue);

        Assert.Equal(OmodValueType.Bool, rows[2].Kind);
        Assert.True(rows[2].BoolValue);
        Assert.Null(rows[2].IntValue);
        Assert.Null(rows[2].FloatValue);

        Assert.Equal(OmodValueType.String, rows[3].Kind);
        Assert.Null(rows[3].IntValue);
        Assert.Null(rows[3].FloatValue);
        Assert.Null(rows[3].BoolValue);
        Assert.Null(rows[3].EnumValue);
        Assert.Null(rows[3].Form);

        Assert.Equal(OmodValueType.FormInt, rows[4].Kind);
        Assert.Equal(K(0x802), rows[4].Form);
        Assert.Equal(441, rows[4].IntValue);
        Assert.Null(rows[4].FloatValue);

        Assert.Equal(OmodValueType.Enum, rows[5].Kind);
        Assert.Equal(4u, rows[5].EnumValue);
        Assert.Null(rows[5].IntValue);
        Assert.Null(rows[5].Form);

        Assert.Equal(OmodValueType.FormFloat, rows[6].Kind);
        Assert.Equal(K(0x802), rows[6].Form);
        Assert.Equal(5f, rows[6].FloatValue);
        Assert.Null(rows[6].IntValue);

        Assert.Null(rows[7].Kind);
        Assert.Null(rows[7].IntValue);
        Assert.Null(rows[7].FloatValue);
        Assert.Null(rows[7].BoolValue);
        Assert.Null(rows[7].EnumValue);
        Assert.Null(rows[7].Form);

        Assert.Equal(OmodValueType.FormInt, rows[8].Kind);
        Assert.Null(rows[8].Form);
        Assert.Equal(7, rows[8].IntValue);
    }

    private static RadicleEnvironment? VanillaEnvironment()
    {
        var dataDir = Environment.GetEnvironmentVariable("FO4_DATA_DIR");
        if (string.IsNullOrEmpty(dataDir)) return null;
        var master = Path.Combine(dataDir, "Fallout4.esm");
        return File.Exists(master) ? RadicleEnvironment.Load(new[] { master }) : null;
    }

    private static FormKey Vanilla(uint id) => new("Fallout4.esm", id);

    [Fact]
    public void VanillaPerksReadTypedEntryPointValues()
    {
        using var env = VanillaEnvironment();
        if (env is null) return;

        var adrenaline = env.PerkEntryPoints(Vanilla(0x811))!.Effects.First(e => e.EntryPoint == 35);
        Assert.Equal(PerkFunctionType.SingleFloat, adrenaline.FunctionType);
        Assert.Equal(BitConverter.UInt32BitsToSingle(0x3F866666u), adrenaline.Value);

        var spell = env.PerkEntryPoints(Vanilla(0x24848E))!.Effects.First(e => e.EntryPoint == 51 && e.Function == 10);
        Assert.Equal(PerkFunctionType.Spell, spell.FunctionType);
        Assert.Equal(Vanilla(0x24848D), spell.Form);
        Assert.Null(spell.Value);

        var damage = env.PerkEntryPoints(Vanilla(0x84B))!.Effects.First(e => e.EntryPoint == 36);
        Assert.Equal((byte)13, damage.Function);
        Assert.Equal(PerkFunctionType.ActorValue, damage.FunctionType);
        Assert.Equal(new PerkActorValue(Vanilla(0x84A), 1f), damage.ActorValue);
        Assert.Null(damage.Value);

        var seen = new[] { false, false, false };
        foreach (var perk in env.WinnersOf("PERK"))
        {
            foreach (var effect in env.PerkEntryPoints(perk).Effects)
            {
                if (effect.ParameterType == 1)
                {
                    Assert.Equal(PerkFunctionType.SingleFloat, effect.FunctionType);
                    Assert.NotNull(effect.Value);
                    seen[0] = true;
                }
                else if (effect.ParameterType == 5)
                {
                    Assert.Equal(PerkFunctionType.Spell, effect.FunctionType);
                    Assert.NotNull(effect.Form);
                    seen[1] = true;
                }
                else if (effect.ParameterType == 8)
                {
                    Assert.Equal(PerkFunctionType.ActorValue, effect.FunctionType);
                    Assert.NotNull(effect.ActorValue?.ActorValue);
                    seen[2] = true;
                }
            }
        }
        Assert.True(seen.All(found => found));
    }

    [Fact]
    public void VanillaObjectModsReadTypedValuesOfEveryValueType()
    {
        using var env = VanillaEnvironment();
        if (env is null) return;

        var laser = env.OmodProperties(Vanilla(0x2492B3))!.Properties.First(p => p.ValueType == 0);
        Assert.Equal(OmodValueType.Int, laser.Kind);
        Assert.Equal((ushort)10, laser.Property);
        Assert.Equal(1, laser.IntValue);

        var scope = env.OmodProperties(Vanilla(0x2492BB))!.Properties;
        var number = scope.First(p => p.Property == 66 && p.ValueType == 1);
        Assert.Equal(BitConverter.UInt32BitsToSingle(0xBE4CCCCDu), number.FloatValue);
        var flag = scope.First(p => p.Property == 48 && p.ValueType == 2);
        Assert.True(flag.BoolValue);
        var linked = scope.First(p => p.Property == 67 && p.ValueType == 4);
        Assert.Equal(Vanilla(0x247A25), linked.Form);
        Assert.Equal(441, linked.IntValue);

        var suppressor = env.OmodProperties(Vanilla(0x1B2900))!.Properties.First(p => p.ValueType == 5);
        Assert.Equal((ushort)59, suppressor.Property);
        Assert.Equal(4u, suppressor.EnumValue);

        var lining = env.OmodProperties(Vanilla(0x245BEA))!.Properties.First(p => p.ValueType == 6);
        Assert.Equal(Vanilla(0x245BEB), lining.Form);
        Assert.Equal(5f, lining.FloatValue);

        var seen = new bool[7];
        foreach (var omod in env.WinnersOf("OMOD"))
        {
            foreach (var row in env.OmodProperties(omod).Properties)
            {
                Assert.True(row.Kind.HasValue);
                seen[row.ValueType] = true;
                switch (row.Kind!.Value)
                {
                    case OmodValueType.Int:
                        Assert.Equal(unchecked((int)row.Value1), row.IntValue);
                        break;
                    case OmodValueType.Float:
                        Assert.NotNull(row.FloatValue);
                        break;
                    case OmodValueType.Bool:
                        Assert.Equal(row.Value1 != 0, row.BoolValue);
                        break;
                    case OmodValueType.FormInt:
                        Assert.Equal(row.Value1 == 0, row.Form is null);
                        Assert.Equal(unchecked((int)row.Value2), row.IntValue);
                        break;
                    case OmodValueType.Enum:
                        Assert.Equal(row.Value1, row.EnumValue);
                        break;
                    case OmodValueType.FormFloat:
                        Assert.Equal(row.Value1 == 0, row.Form is null);
                        Assert.NotNull(row.FloatValue);
                        break;
                }
            }
        }
        foreach (var type in new[] { 0, 1, 2, 4, 5, 6 }) Assert.True(seen[type], $"no vanilla OMOD property of value type {type}");
    }
}
