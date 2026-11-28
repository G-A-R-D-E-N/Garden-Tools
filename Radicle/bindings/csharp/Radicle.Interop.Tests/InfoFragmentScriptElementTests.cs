using System;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public sealed class InfoFragmentScriptElementTests
{
    private static byte[] Cat(params byte[][] parts) => parts.SelectMany(p => p).ToArray();

    private static byte[] U16(params ushort[] values) => values.SelectMany(BitConverter.GetBytes).ToArray();

    private static byte[] WString(string text) => Cat(U16((ushort)text.Length), System.Text.Encoding.ASCII.GetBytes(text));

    private static byte[] InfoVmad(byte[] properties, ushort propertyCount) => Cat(
        U16(5, 2, 0),
        new byte[] { 3, 0 }, WString("PF_Info"), new byte[] { 0 }, U16(propertyCount), properties);

    private static byte[] SupportedProperties() => Cat(
        WString("Targets"), new byte[] { 11, 1 }, U32(2), U16(0, 0xFFFF), U32(0x805), U16(0, 3), U32(0x806),
        WString("Labels"), new byte[] { 12, 0 }, U32(2), WString(""), WString("Alpha"),
        WString("Counts"), new byte[] { 13, 0 }, U32(3, 4, 0xFFFFFFFEu, 6),
        WString("Point"), new byte[] { 7, 1 }, U32(2),
        WString("Target"), new byte[] { 1, 0 }, U16(0, 3), U32(0x806),
        WString("Label"), new byte[] { 2, 0 }, WString("Alpha"),
        WString("Pairs"), new byte[] { 17, 0 }, U32(2), U32(1), WString("Flag"), new byte[] { 5, 0, 1 }, U32(0),
        WString("Plain"), new byte[] { 3, 0 }, U32(9));

    private static byte[] ArrayMemberProperties() => Cat(
        WString("Holder"), new byte[] { 7, 0 }, U32(1),
        WString("List"), new byte[] { 13, 0 }, U32(1), U32(5),
        WString("After"), new byte[] { 3, 0 }, U32(13));

    private static byte[] DynamicValueProperties() => Cat(
        WString("Before"), new byte[] { 3, 0 }, U32(11),
        WString("DynamicValue"), new byte[] { 6, 0 },
        WString("After"), new byte[] { 3, 0 }, U32(13));

    private static DialogueInfoResult Read(TempDirectory directory, byte[] vmad)
    {
        var path = directory.Write("InfoScripts.esm", Build(1, Array.Empty<string>(),
            Top("INFO", new Rec("INFO", 0x800, new[] { ("VMAD", vmad) }))));
        using var env = RadicleEnvironment.Load(new[] { path });
        return env.DialogueInfoResult(env.Winner(new FormKey("InfoScripts.esm", 0x800))!.Value);
    }

    [Fact]
    public void FragmentScriptReportsArrayAndStructElements()
    {
        using var directory = new TempDirectory();
        var result = Read(directory, InfoVmad(SupportedProperties(), 6));
        Assert.Equal(InfoFragmentState.Present, result.FragmentState);
        var script = result.FragmentScript!;
        Assert.False(script.Unsupported);
        Assert.Equal(new[] { "Targets", "Labels", "Counts", "Point", "Pairs", "Plain" }, script.Properties.Select(p => p.Name));

        var targets = script.Properties[0];
        Assert.Equal((VmadValueKind.Object, 2, 2u), (targets.ElementKind, targets.ElementTotal, targets.DeclaredElementCount));
        Assert.Equal(new[] { (0x805u, (short)-1), (0x806u, (short)3) }, targets.Elements.Select(e => (e.Value.ObjectFormId, e.Value.Alias)));

        var labels = script.Properties[1];
        Assert.Equal(VmadValueKind.Text, labels.ElementKind);
        Assert.Equal(new[] { "", "Alpha" }, labels.Elements.Select(e => e.Value.Text!));

        var counts = script.Properties[2];
        Assert.Equal(VmadValueKind.Int, counts.ElementKind);
        Assert.Equal(new[] { 4, -2, 6 }, counts.Elements.Select(e => e.Value.Integer));

        var point = script.Properties[3];
        Assert.Equal((VmadValueKind.StructMember, 2, 2u), (point.ElementKind, point.ElementTotal, point.DeclaredElementCount));
        Assert.Equal(new[] { "Target", "Label" }, point.Elements.Select(e => e.MemberName!));
        Assert.Equal(new[] { VmadValueKind.Object, VmadValueKind.Text }, point.Elements.Select(e => e.Kind));
        Assert.Equal((0x806u, (short)3), (point.Elements[0].Value.ObjectFormId, point.Elements[0].Value.Alias));
        Assert.Equal("Alpha", point.Elements[1].Value.Text);

        var pairs = script.Properties[4];
        Assert.Equal((VmadValueKind.StructMember, 1, 2u), (pairs.ElementKind, pairs.ElementTotal, pairs.DeclaredElementCount));
        var flag = Assert.Single(pairs.Elements);
        Assert.Equal(("Flag", VmadValueKind.Bool, 0, true), (flag.MemberName!, flag.Kind, flag.StructIndex, flag.Value.Boolean));

        var plain = script.Properties[5];
        Assert.Equal((VmadValueKind.None, 0), (plain.ElementKind, plain.ElementTotal));
        Assert.Empty(plain.Elements);
        Assert.Equal(9, plain.Value.Integer);
    }

    [Fact]
    public void FragmentScriptReadsAStructMemberThatIsAnArray()
    {
        using var directory = new TempDirectory();
        var result = Read(directory, InfoVmad(ArrayMemberProperties(), 2));
        Assert.Equal(InfoFragmentState.Present, result.FragmentState);
        var script = result.FragmentScript!;
        Assert.False(script.Unsupported);
        var holder = script.Properties[0];
        Assert.Equal(("Holder", (byte)7, 1u, 1), (holder.Name, holder.Type, holder.DeclaredElementCount, holder.ElementTotal));
        var list = Assert.Single(holder.Elements);
        Assert.Equal(5, Assert.Single(list.Children!).Value.Integer);
        Assert.Equal(13, script.Properties[1].Value.Integer);
    }

    [Fact]
    public void UndecodablePropertyLeavesTheFragmentSectionUnreadable()
    {
        using var directory = new TempDirectory();
        var result = Read(directory, InfoVmad(DynamicValueProperties(), 3));
        Assert.Equal(InfoFragmentState.Unreadable, result.FragmentState);
        Assert.Null(result.FragmentScript);
    }
}
