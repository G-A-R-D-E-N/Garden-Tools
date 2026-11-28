using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public sealed class VmadScriptRowTests
{
    private static byte[] Cat(params byte[][] parts) => parts.SelectMany(p => p).ToArray();

    private static byte[] U16(params ushort[] values) => values.SelectMany(BitConverter.GetBytes).ToArray();

    private static byte[] WString(string text) => Cat(U16((ushort)text.Length), System.Text.Encoding.ASCII.GetBytes(text));

    private static readonly float NegativeZero = BitConverter.Int32BitsToSingle(unchecked((int)0x80000000));

    private static byte[] ArrayVmad() => Cat(
        U16(6, 2, 1),
        WString("ArrayScript"), new byte[] { 1 }, U16(10),
        WString("Targets"), new byte[] { 11, 1 }, U32(2), U16(0, 0xFFFF), U32(0x805), U16(0, 3), U32(0x806),
        WString("Labels"), new byte[] { 12, 0 }, U32(2), WString(""), WString("Alpha"),
        WString("Counts"), new byte[] { 13, 0 }, U32(3, 4, 0xFFFFFFFEu, 6),
        WString("Ratios"), new byte[] { 14, 0 }, U32(2), F32(1.5f, NegativeZero),
        WString("Switches"), new byte[] { 15, 0 }, U32(3), new byte[] { 0, 1, 2 },
        WString("Owner"), new byte[] { 1, 0 }, U16(0, 3), U32(0x806),
        WString("Title"), new byte[] { 2, 0 }, WString("Hello"),
        WString("Level"), new byte[] { 3, 0 }, U32(0xFFFFFFF9u),
        WString("Scale"), new byte[] { 4, 0 }, F32(2.25f),
        WString("Enabled"), new byte[] { 5, 0, 1 });

    private static byte[] StructVmad() => Cat(
        U16(6, 2, 1),
        WString("StructScript"), new byte[] { 0 }, U16(2),
        WString("Point"), new byte[] { 7, 1 }, U32(3),
        WString("Target"), new byte[] { 1, 0 }, U16(0, 3), U32(0x806),
        WString("Label"), new byte[] { 2, 0 }, WString("Alpha"),
        WString("Count"), new byte[] { 3, 0 }, U32(0xFFFFFFFCu),
        WString("Pairs"), new byte[] { 17, 0 }, U32(2), U32(1), WString("Flag"), new byte[] { 5, 0, 1 }, U32(0));

    private static byte[] UnsupportedVmad() => Cat(
        U16(6, 2, 2),
        WString("VariableScript"), new byte[] { 0 }, U16(3),
        WString("Before"), new byte[] { 3, 1 }, U32(11),
        WString("DynamicValue"), new byte[] { 6, 1 },
        WString("After"), new byte[] { 3, 1 }, U32(13),
        WString("LaterScript"), new byte[] { 0 }, U16(0));

    private static byte[] ArrayMemberVmad() => Cat(
        U16(6, 2, 1),
        WString("MemberScript"), new byte[] { 0 }, U16(2),
        WString("Holder"), new byte[] { 7, 0 }, U32(1),
        WString("List"), new byte[] { 13, 0 }, U32(1), U32(5),
        WString("After"), new byte[] { 3, 0 }, U32(13));

    private static byte[] NestedVmad() => Cat(
        U16(6, 2, 3),
        WString("First"), new byte[] { 0 }, U16(1),
        WString("Groups"), new byte[] { 17, 1 }, U32(2),
        U32(2), WString("Id"), new byte[] { 3, 0 }, U32(9),
        WString("Inner"), new byte[] { 7, 0 }, U32(1), WString("Names"), new byte[] { 12, 0 }, U32(2), WString("a"), WString("b"),
        U32(0),
        WString("Second"), new byte[] { 0 }, U16(2),
        WString("Fine"), new byte[] { 3, 0 }, U32(1),
        WString("Dynamic"), new byte[] { 6, 0 });

    private static string Load(TempDirectory directory, string name, byte[] vmad, out RadicleEnvironment env, out Record record)
    {
        var path = directory.Write(name, Build(0, Array.Empty<string>(),
            Top("REFR", new Rec("REFR", 0x800, new[] { ("VMAD", vmad) }), new Rec("REFR", 0x801, Array.Empty<(string, byte[])>())),
            Top("STAT", new Rec("STAT", 0x806, Array.Empty<(string, byte[])>()))));
        env = RadicleEnvironment.Load(new[] { path });
        record = env.Winner(new FormKey(name, 0x800))!.Value;
        return path;
    }

    private static string Value(VmadPropertyValue value) => string.Join("|",
        value.Kind, value.ObjectFormId.ToString("X", CultureInfo.InvariantCulture), value.Alias, value.Text ?? "<null>", value.Integer,
        BitConverter.SingleToInt32Bits(value.Real), value.Boolean);

    private static List<string> Dump(IReadOnlyList<VmadScriptInfo> scripts)
    {
        var lines = new List<string>();
        foreach (var script in scripts)
        {
            lines.Add($"script {script.Name} {script.Flags} {script.Unsupported} {script.TrailingBytes} {script.Properties.Count} {script.Stop}");
            foreach (var property in script.Properties)
            {
                lines.Add($"property {property.Name} {property.Type} {property.Flags} {Value(property.Value)} {property.DeclaredElementCount} {property.ElementKind} {property.ElementTotal}");
                foreach (var element in property.Elements) DumpElement(lines, element, "element");
            }
        }
        return lines;
    }

    private static void DumpElement(List<string> lines, VmadElementInfo element, string label)
    {
        lines.Add($"{label} {element.Kind} {element.MemberName ?? "<null>"} {element.StructIndex} {element.IndexInProperty} {element.Type} {element.Flags} {Value(element.Value)} {element.Children?.Count.ToString(CultureInfo.InvariantCulture) ?? "<none>"}");
        foreach (var child in element.Children ?? Array.Empty<VmadElementInfo>()) DumpElement(lines, child, label + ">");
    }

    [Fact]
    public void ArraysOfEveryElementKindAndScalarsAreLossless()
    {
        using var directory = new TempDirectory();
        Load(directory, "Arrays.esp", ArrayVmad(), out var env, out var record);
        using var lease = env;
        Assert.True(env.TryScripts(record, out var scripts));
        var script = Assert.Single(scripts);
        Assert.Equal("ArrayScript", script.Name);
        Assert.Equal((byte)1, script.Flags);
        Assert.False(script.Unsupported);
        Assert.Equal(0L, script.TrailingBytes);
        var properties = script.Properties;
        Assert.Equal(10, properties.Count);

        var targets = properties[0];
        Assert.Equal(("Targets", (byte)11, (byte)1, 2u, VmadValueKind.Object, 2), (targets.Name, targets.Type, targets.Flags, targets.DeclaredElementCount, targets.ElementKind, targets.ElementTotal));
        Assert.Equal(VmadValueKind.None, targets.Value.Kind);
        Assert.Equal(new[] { (VmadValueKind.Object, 0u, 0x805u, (short)-1), (VmadValueKind.Object, 1u, 0x806u, (short)3) },
                     targets.Elements.Select(e => (e.Kind, (uint)e.IndexInProperty, e.Value.ObjectFormId, e.Value.Alias)));
        Assert.All(targets.Elements, e =>
        {
            Assert.Null(e.MemberName);
            Assert.Null(e.Value.Text);
            Assert.Equal(0, e.StructIndex);
            Assert.Equal((byte)0, e.Type);
        });

        var labels = properties[1];
        Assert.Equal((VmadValueKind.Text, 2, 2u), (labels.ElementKind, labels.ElementTotal, labels.DeclaredElementCount));
        Assert.Equal(new[] { (VmadValueKind.Text, ""), (VmadValueKind.Text, "Alpha") }, labels.Elements.Select(e => (e.Kind, e.Value.Text!)));

        var counts = properties[2];
        Assert.Equal((VmadValueKind.Int, 3, 3u), (counts.ElementKind, counts.ElementTotal, counts.DeclaredElementCount));
        Assert.Equal(new[] { 4, -2, 6 }, counts.Elements.Select(e => e.Value.Integer));
        Assert.All(counts.Elements, e => Assert.Equal(VmadValueKind.Int, e.Kind));
        Assert.All(counts.Elements, e => Assert.Null(e.Value.Text));

        var ratios = properties[3];
        Assert.Equal((VmadValueKind.Real, 2, 2u), (ratios.ElementKind, ratios.ElementTotal, ratios.DeclaredElementCount));
        Assert.Equal(1.5f, ratios.Elements[0].Value.Real);
        Assert.Equal(unchecked((int)0x80000000), BitConverter.SingleToInt32Bits(ratios.Elements[1].Value.Real));
        Assert.All(ratios.Elements, e => Assert.Equal(VmadValueKind.Real, e.Kind));

        var switches = properties[4];
        Assert.Equal((VmadValueKind.Bool, 3, 3u), (switches.ElementKind, switches.ElementTotal, switches.DeclaredElementCount));
        Assert.Equal(new[] { false, true, true }, switches.Elements.Select(e => e.Value.Boolean));
        Assert.All(switches.Elements, e => Assert.Equal(VmadValueKind.Bool, e.Kind));

        foreach (var scalar in properties.Skip(5))
        {
            Assert.Equal(VmadValueKind.None, scalar.ElementKind);
            Assert.Equal(0, scalar.ElementTotal);
            Assert.Empty(scalar.Elements);
        }
        Assert.Equal((VmadValueKind.Object, 0x806u, (short)3), (properties[5].Value.Kind, properties[5].Value.ObjectFormId, properties[5].Value.Alias));
        Assert.Equal((VmadValueKind.Text, "Hello"), (properties[6].Value.Kind, properties[6].Value.Text!));
        Assert.Equal((VmadValueKind.Int, -7), (properties[7].Value.Kind, properties[7].Value.Integer));
        Assert.Equal((VmadValueKind.Real, 2.25f), (properties[8].Value.Kind, properties[8].Value.Real));
        Assert.Equal((VmadValueKind.Bool, true), (properties[9].Value.Kind, properties[9].Value.Boolean));
    }

    [Fact]
    public void StructPropertyAndStructArrayExposeMemberRows()
    {
        using var directory = new TempDirectory();
        Load(directory, "Structs.esp", StructVmad(), out var env, out var record);
        using var lease = env;
        var script = Assert.Single(env.Scripts(record));
        Assert.False(script.Unsupported);

        var point = script.Properties[0];
        Assert.Equal(("Point", (byte)7, (byte)1, 3u, VmadValueKind.StructMember, 3), (point.Name, point.Type, point.Flags, point.DeclaredElementCount, point.ElementKind, point.ElementTotal));
        Assert.Equal(new[] { "Target", "Label", "Count" }, point.Elements.Select(e => e.MemberName!));
        Assert.Equal(new[] { VmadValueKind.Object, VmadValueKind.Text, VmadValueKind.Int }, point.Elements.Select(e => e.Kind));
        Assert.Equal(new[] { 0, 1, 2 }, point.Elements.Select(e => e.IndexInProperty));
        Assert.Equal(new byte[] { 1, 2, 3 }, point.Elements.Select(e => e.Type));
        Assert.All(point.Elements, e => Assert.Equal(0, e.StructIndex));
        Assert.Equal((0x806u, (short)3), (point.Elements[0].Value.ObjectFormId, point.Elements[0].Value.Alias));
        Assert.Equal("Alpha", point.Elements[1].Value.Text);
        Assert.Equal(-4, point.Elements[2].Value.Integer);
        Assert.Equal((FormKey?)new FormKey("Structs.esp", 0x806), env.ResolveFormId(record, point.Elements[0].Value.ObjectFormId));

        var pairs = script.Properties[1];
        Assert.Equal(("Pairs", (byte)17, 2u, VmadValueKind.StructMember, 1), (pairs.Name, pairs.Type, pairs.DeclaredElementCount, pairs.ElementKind, pairs.ElementTotal));
        var flag = Assert.Single(pairs.Elements);
        Assert.Equal(("Flag", VmadValueKind.Bool, 0, (byte)5, true), (flag.MemberName!, flag.Kind, flag.StructIndex, flag.Type, flag.Value.Boolean));
    }

    [Fact]
    public void UndecodableVariableCutsTheReadAndFlagsEveryScript()
    {
        using var directory = new TempDirectory();
        Load(directory, "Variable.esp", UnsupportedVmad(), out var env, out var record);
        using var lease = env;
        var script = Assert.Single(env.Scripts(record));
        Assert.True(script.Unsupported);
        Assert.Equal(0L, script.TrailingBytes);
        Assert.Equal("VariableScript", script.Name);
        var before = Assert.Single(script.Properties);
        Assert.Equal(("Before", 11), (before.Name, before.Value.Integer));
        Assert.Equal(new VmadReadStop(0, "DynamicValue", 6, 2), script.Stop);
    }

    [Fact]
    public void StructWithAnArrayMemberExposesItsElementsAsChildren()
    {
        using var directory = new TempDirectory();
        Load(directory, "Member.esp", ArrayMemberVmad(), out var env, out var record);
        using var lease = env;
        var script = Assert.Single(env.Scripts(record));
        Assert.False(script.Unsupported);
        Assert.Null(script.Stop);
        Assert.Equal(2, script.Properties.Count);
        var holder = script.Properties[0];
        Assert.Equal(("Holder", (byte)7, 1u, 1), (holder.Name, holder.Type, holder.DeclaredElementCount, holder.ElementTotal));
        var list = Assert.Single(holder.Elements);
        Assert.Equal(("List", (byte)13, VmadValueKind.None), (list.MemberName!, list.Type, list.Kind));
        var item = Assert.Single(list.Children!);
        Assert.Equal((VmadValueKind.Int, 5, (byte)3, 0), (item.Kind, item.Value.Integer, item.Type, item.IndexInProperty));
        Assert.Null(item.Children);
        Assert.Null(item.MemberName);
        Assert.Equal(13, script.Properties[1].Value.Integer);
    }

    [Fact]
    public void NestedStructArrayKeepsEveryLevelAndNamesTheStopInALaterScript()
    {
        using var directory = new TempDirectory();
        Load(directory, "Nested.esp", NestedVmad(), out var env, out var record);
        using var lease = env;
        var scripts = env.Scripts(record);
        Assert.Equal(2, scripts.Count);
        Assert.All(scripts, s => Assert.Equal(new VmadReadStop(1, "Dynamic", 6, 3), s.Stop));
        var groups = scripts[0].Properties[0];
        Assert.Equal(("Groups", (byte)17, 2u, 2), (groups.Name, groups.Type, groups.DeclaredElementCount, groups.ElementTotal));
        var inner = groups.Elements[1];
        Assert.Equal(("Inner", (byte)7, 0), (inner.MemberName!, inner.Type, inner.StructIndex));
        var names = Assert.Single(inner.Children!);
        Assert.Equal(("Names", (byte)12), (names.MemberName!, names.Type));
        Assert.Equal(new[] { "a", "b" }, names.Children!.Select(c => c.Value.Text!));
        Assert.Equal(new[] { 0, 1 }, names.Children!.Select(c => c.IndexInProperty));
        var second = scripts[1];
        Assert.Equal(("Second", 1), (second.Name, second.Properties.Count));
    }

    [Fact]
    public void RecordWithoutVmadReportsFalse()
    {
        using var directory = new TempDirectory();
        Load(directory, "Plain.esp", ArrayVmad(), out var env, out _);
        using var lease = env;
        var plain = env.Winner(new FormKey("Plain.esp", 0x801))!.Value;
        Assert.False(env.TryScripts(plain, out var scripts));
        Assert.Empty(scripts);
        Assert.Throws<KeyNotFoundException>(() => env.Scripts(plain));
    }

    [Theory]
    [InlineData("Arrays")]
    [InlineData("Structs")]
    [InlineData("Variable")]
    [InlineData("Member")]
    [InlineData("Nested")]
    public void EnvironmentAndPluginReadsAgreeOnEveryRow(string name)
    {
        var vmad = name switch { "Arrays" => ArrayVmad(), "Structs" => StructVmad(), "Variable" => UnsupportedVmad(), "Nested" => NestedVmad(), _ => ArrayMemberVmad() };
        using var directory = new TempDirectory();
        var path = Load(directory, name + ".esp", vmad, out var env, out var record);
        using var lease = env;
        using var plugin = PluginFile.Read(path);
        Assert.True(env.TryScripts(record, out var fromEnvironment));
        Assert.True(plugin.TryScripts(new FormKey(name + ".esp", 0x800), out var fromPlugin));
        Assert.Equal(Dump(fromEnvironment), Dump(fromPlugin));
    }
}
