using System;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public sealed class PlacedScriptElementTests
{
    private static byte[] Cat(params byte[][] parts) => parts.SelectMany(p => p).ToArray();

    private static byte[] U16(params ushort[] values) => values.SelectMany(BitConverter.GetBytes).ToArray();

    private static byte[] WString(string text) => Cat(U16((ushort)text.Length), System.Text.Encoding.ASCII.GetBytes(text));

    private static byte[] Vmad() => Cat(
        U16(6, 2, 1),
        WString("ArrayScript"), new byte[] { 0 }, U16(5),
        WString("Targets"), new byte[] { 11, 1 }, U32(2),
        U16(0, 0xFFFF), U32(0x805),
        U16(0, 3), U32(0x806),
        WString("Labels"), new byte[] { 12, 1 }, U32(2), WString(""), WString("Alpha"),
        WString("Ratios"), new byte[] { 14, 1 }, U32(2), F32(1.5f, BitConverter.Int32BitsToSingle(unchecked((int)0x80000000))),
        WString("Switches"), new byte[] { 15, 1 }, U32(3), new byte[] { 0, 1, 2 },
        WString("Count"), new byte[] { 3, 1 }, U32(7));

    private static byte[] StructVmad() => Cat(
        U16(6, 2, 1),
        WString("StructScript"), new byte[] { 0 }, U16(2),
        WString("Point"), new byte[] { 7, 1 }, U32(3),
        WString("Target"), new byte[] { 1, 0 }, U16(0, 3), U32(0x806),
        WString("Label"), new byte[] { 2, 0 }, WString("Alpha"),
        WString("Count"), new byte[] { 3, 0 }, U32(0xFFFFFFFCu),
        WString("Pairs"), new byte[] { 17, 0 }, U32(2), U32(1), WString("Flag"), new byte[] { 5, 0, 1 }, U32(0));

    [Fact]
    public void ExposesDecodedStructMembersAndStructArrays()
    {
        using var directory = new TempDirectory();
        var path = directory.Write("Structs.esp", Build(0, Array.Empty<string>(),
            Top("REFR", new Rec("REFR", 0x800, new[] { ("VMAD", StructVmad()) })),
            Top("STAT", new Rec("STAT", 0x806, Array.Empty<(string, byte[])>()))));
        using var env = RadicleEnvironment.Load(new[] { path });
        var placed = env.PlacedReference(env.Winner(new FormKey("Structs.esp", 0x800))!.Value);
        var properties = placed.Scripts.Single().Properties;

        var point = properties[0].Structs!.Single();
        Assert.Equal(new[] { "Target", "Label", "Count" }, point.Members.Select(m => m.Name));
        Assert.Equal(new byte[] { 1, 2, 3 }, point.Members.Select(m => m.Type));
        Assert.Equal(new FormKey?[] { new FormKey("Structs.esp", 0x806), null, null },
                     point.Members.Select(m => m.Value.Object));
        Assert.Equal(new short[] { 3, 0, 0 }, point.Members.Select(m => m.Value.Alias));
        Assert.Equal("Alpha", point.Members[1].Value.Text);
        Assert.Equal(-4, point.Members[2].Value.Integer);
        Assert.Null(properties[0].Elements);

        var pairs = properties[1].Structs!;
        Assert.Equal(2, pairs.Count);
        Assert.Equal("Flag", pairs[0].Members.Single().Name);
        Assert.True(pairs[0].Members.Single().Value.Boolean);
        Assert.Empty(pairs[1].Members);
    }

    [Fact]
    public void ExposesDecodedArrayElementsAndNullForScalars()
    {
        using var directory = new TempDirectory();
        var path = directory.Write("Elements.esp", Build(0, Array.Empty<string>(),
            Top("REFR", new Rec("REFR", 0x800, new[] { ("VMAD", Vmad()) })),
            Top("ARMO", new Rec("ARMO", 0x805, Array.Empty<(string, byte[])>())),
            Top("STAT", new Rec("STAT", 0x806, Array.Empty<(string, byte[])>()))));
        using var env = RadicleEnvironment.Load(new[] { path });
        var placed = env.PlacedReference(env.Winner(new FormKey("Elements.esp", 0x800))!.Value);
        var properties = placed.Scripts.Single().Properties;

        var targets = properties[0].Elements;
        Assert.NotNull(targets);
        Assert.Equal(new FormKey?[] { new FormKey("Elements.esp", 0x805), new FormKey("Elements.esp", 0x806) },
                     targets!.Select(e => e.Object));
        Assert.Equal(new short[] { -1, 3 }, targets.Select(e => e.Alias));
        Assert.All(targets, e => Assert.Null(e.Text));

        var labels = properties[1].Elements;
        Assert.NotNull(labels);
        Assert.Equal(new string?[] { "", "Alpha" }, labels!.Select(e => e.Text));
        Assert.All(labels, e => Assert.Null(e.Object));

        var ratios = properties[2].Elements;
        Assert.NotNull(ratios);
        Assert.Equal(1.5f, ratios![0].Real);
        Assert.Equal(unchecked((int)0x80000000), BitConverter.SingleToInt32Bits(ratios[1].Real));

        var switches = properties[3].Elements;
        Assert.NotNull(switches);
        Assert.Equal(new[] { false, true, true }, switches!.Select(e => e.Boolean));

        Assert.Equal(7, properties[4].Integer);
        Assert.Null(properties[4].Elements);
    }
}
