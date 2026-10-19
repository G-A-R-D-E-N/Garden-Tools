using System;
using System.Linq;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class BehaviorTests
{
    private static byte[] Floats(int count) => F32(Enumerable.Range(0, count).Select(i => i + 0.5f).ToArray());
    private static byte[] Condition(ushort function) => new byte[] { 0xE1, 0, 0, 0 }.Concat(U32(0)).Concat(BitConverter.GetBytes(function))
        .Concat(new byte[2]).Concat(U32(unchecked((uint)-7), 0, 0, 0, uint.MaxValue)).ToArray();

    [Fact]
    public void PackageGetterCopiesEncodedValuesScopedLinksAndOpaqueInputConditions()
    {
        using var temp = new TempDirectory();
        var data = U32(0x80000001).Concat(new byte[] { 0xFE, 7, 3, 0xAB, 1, 0x80, 0xAD, 0xDE }).ToArray();
        var schedule = new byte[] { 255, 255, 255, 255, 255, 0, 0, 0 }.Concat(U32(uint.MaxValue)).ToArray();
        var path = temp.Write("Behavior.esm", Build(1, Array.Empty<string>(), Top("PACK", new Rec("PACK", 0x800, new[] {
            ("PKDT", data), ("PSDT", schedule), ("CNAM", U32(0x810)), ("QNAM", U32(0x811)), ("CTDA", Condition(14)),
            ("CIS1", Z("operand")), ("PKCU", U32(4, 0x812, 9)), ("ANAM", Z("unknown-input")), ("CNAM", U32(0xAABBCCDD)),
            ("CTDA", Condition(15)), ("POBA", Array.Empty<byte>()), ("INAM", U32(0x813)), ("POEA", Array.Empty<byte>()),
            ("INAM", U32(0x814)), ("POCA", Array.Empty<byte>()), ("INAM", U32(0)) }))));
        Package package;
        using (var env = RadicleEnvironment.Load(new[] { path })) package = env.Package(env.Winner(new FormKey("Behavior.esm", 0x800))!.Value);
        Assert.Equal((byte)0xFE, package.Data!.Type); Assert.Equal((ushort)0x8001, package.Data.InterruptFlags);
        Assert.Equal((sbyte)-1, package.Schedule!.Month); Assert.Equal((sbyte)-1, package.Schedule.Minute); Assert.Equal(uint.MaxValue, package.Schedule.DurationMinutes);
        Assert.Equal((ushort)0xDEAD, package.Data.TrailingValue);
        Assert.Equal(new FormKey("Behavior.esm", 0x812), package.Counter!.TemplatePackage); Assert.Equal(new FormKey("Behavior.esm", 0x810), package.CombatStyle);
        Assert.Equal(new FormKey("Behavior.esm", 0x813), package.BeginIdle); Assert.Equal(new FormKey("Behavior.esm", 0x814), package.EndIdle); Assert.Null(package.ChangeIdle);
        Assert.Equal((ushort)14, Assert.Single(package.Conditions).Function); Assert.Equal("operand", package.Conditions[0].Parameter1String);
        Assert.Equal("CNAM", package.Opaque[1].Signature); Assert.Equal(U32(0xAABBCCDD), package.Opaque[1].Bytes);
    }

    [Theory]
    [InlineData(9, 3)]
    [InlineData(9, 4)]
    [InlineData(9, 5)]
    [InlineData(10, 3)]
    [InlineData(10, 4)]
    [InlineData(10, 5)]
    public void CombatStyleGetterCopiesAllStoredTailVariantsAndIntegerSlot(int meleeCount, int rangeCount)
    {
        using var temp = new TempDirectory();
        var close = Floats(9).Concat(U32(0xFFFFFFFE)).Concat(F32(0.5f)).ToArray();
        var path = temp.Write("Behavior.esm", Build(1, Array.Empty<string>(), Top("CSTY", new Rec("CSTY", 0x800,
            new[] { ("CSGD", Floats(12)), ("CSME", Floats(meleeCount)), ("CSLR", Floats(rangeCount)), ("CSCR", close),
                ("CSFL", Floats(8)), ("CSRA", F32(0.75f)), ("CSCV", F32(0.25f)), ("DATA", U32(0x80000001)) }))));
        CombatStyle style;
        using (var env = RadicleEnvironment.Load(new[] { path })) style = env.CombatStyle(env.Winner(new FormKey("Behavior.esm", 0x800))!.Value);
        Assert.Equal(meleeCount, style.Melee.Length); Assert.Equal(rangeCount, style.LongRange.Length); Assert.Equal(11.5f, style.General![11]);
        Assert.Equal(7.5f, style.Flight![7]); Assert.Equal(0xFFFFFFFEu, style.CloseRange!.ThrowMaxTargets); Assert.Equal(0.5f, style.CloseRange.FlankVariance);
        Assert.Equal(0x80000001u, style.Flags); Assert.Equal(0.75f, style.RangedAccuracy); Assert.Equal(0.25f, style.CoverSearch);
    }

    [Fact]
    public void PackageLocationAndTargetCopiesRetainIndicesRawValuesAndOpaqueBytes()
    {
        using var temp = new TempDirectory();
        var location = U32(6, 0x810, uint.MaxValue);
        var path = temp.Write("Behavior.esm", Build(1, Array.Empty<string>(), Top("PACK", new Rec("PACK", 0x800, new[] {
            ("IDLA", U32(0x810, 0)), ("PKCU", U32(0, 0, 9)), ("PLDT", location), ("PLDT", U32(14, 99, 7, uint.MaxValue)),
            ("PTDA", U32(7, 0x811, uint.MaxValue)), ("PTDA", U32(4, 33, 0x80000000)), ("PLDT", U32(255, 1, 2)) }))));
        Package package;
        using (var env = RadicleEnvironment.Load(new[] { path })) package = env.Package(env.Winner(new FormKey("Behavior.esm", 0x800))!.Value);
        Assert.Equal(2, package.Locations.Count); Assert.Equal((nuint)2, package.Locations[0].SubrecordIndex);
        Assert.Null(package.Locations[0].ItemIndex); Assert.Equal(new FormKey("Behavior.esm", 0x810), package.Locations[0].Form);
        Assert.Equal(-1, package.Locations[0].Radius); Assert.Equal(uint.MaxValue, package.Locations[1].ItemIndex);
        Assert.Null(package.Locations[1].Form); Assert.Equal(99u, package.Locations[1].Value);
        Assert.Equal(2, package.Targets.Count); Assert.Equal(-1, package.Targets[0].CountOrDistance);
        Assert.Equal(new FormKey("Behavior.esm", 0x811), package.Targets[0].Form); Assert.Null(package.Targets[1].Form);
        Assert.Equal(int.MinValue, package.Targets[1].CountOrDistance); Assert.Equal(location, package.Opaque[0].Bytes);
        Assert.Equal(5, package.Opaque.Count);
        Assert.Equal(2, package.IdleAnimations.Count); Assert.Equal(new FormKey("Behavior.esm", 0x810), package.IdleAnimations[0]);
        Assert.Null(package.IdleAnimations[1]);
    }

    [Fact]
    public void IdleGetterCopiesStringsConditionsAndSelectionBytes()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Behavior.esm", Build(1, Array.Empty<string>(), Top("IDLE", new Rec("IDLE", 0x800, new[] {
            ("ANAM", U32(0x810, 0)), ("DATA", new byte[] { 255, 254, 0x81, 9, 0x34, 0x12 }), ("DNAM", Z("graph.hkx")),
            ("ENAM", Z("event")), ("GNAM", Z("animation.hkx")), ("CTDA", Condition(14)), ("CIS1", Z("operand")) }))));
        Idle idle;
        using (var env = RadicleEnvironment.Load(new[] { path })) idle = env.Idle(env.Winner(new FormKey("Behavior.esm", 0x800))!.Value);
        Assert.Equal(new IdleData(255, 254, 0x81, 9, 0x1234), idle.Data); Assert.Equal(new FormKey("Behavior.esm", 0x810), idle.Parent);
        Assert.Null(idle.Previous); Assert.Equal("graph.hkx", idle.BehaviorGraph); Assert.Equal("event", idle.AnimationEvent); Assert.Equal("animation.hkx", idle.AnimationFile);
        var c = Assert.Single(idle.Conditions); Assert.Equal((ushort)14, c.Function); Assert.Equal(-7, c.Param1); Assert.Equal("operand", c.Parameter1String);
    }

    [Fact]
    public void PackageProcedureGetterCopiesInputsNodesLeafRangesAndNameTables()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Behavior.esm", Build(1, Array.Empty<string>(), Top("PACK", new Rec("PACK", 0x800, new[] {
            ("PKCU", U32(2, 0, 9)), ("ANAM", Z("Int")), ("CNAM", U32(5)), ("ANAM", Z("Bool")), ("CNAM", new byte[] { 1 }),
            ("UNAM", new byte[] { 0 }), ("UNAM", new byte[] { 1 }), ("XNAM", new byte[] { 7 }),
            ("ANAM", Z("Sequence")), ("CITC", U32(1)), ("CTDA", Condition(14)), ("PRCB", U32(1, 0)), ("ANAM", Z("Procedure")),
            ("CITC", U32(0)), ("PNAM", Z("Travel")), ("FNAM", U32(1)), ("UNAM", new byte[] { 0 }), ("BNAM", Z("Target")),
            ("PNAM", U32(1)), ("POBA", Array.Empty<byte>()) }))));
        PackageProcedureTree tree;
        using (var env = RadicleEnvironment.Load(new[] { path })) tree = env.PackageProcedure(env.Winner(new FormKey("Behavior.esm", 0x800))!.Value);
        Assert.True(tree.Decoded); Assert.Equal((nuint)1, tree.RegionBegin); Assert.Equal((nuint)19, tree.RegionEnd); Assert.Equal((byte)7, tree.NextInputId);
        Assert.Equal(2, tree.Inputs.Count); Assert.Equal("Int", tree.Inputs[0].TypeName); Assert.Equal((nuint)1, tree.Inputs[0].SubrecordBegin);
        Assert.Equal((nuint)3, tree.Inputs[0].SubrecordEnd); Assert.Equal((byte)0, tree.Inputs[0].Index);
        Assert.Equal("Bool", tree.Inputs[1].TypeName); Assert.Equal((byte)1, tree.Inputs[1].Index);
        Assert.Equal(2, tree.Nodes.Count);
        Assert.False(tree.Nodes[0].IsLeaf); Assert.Equal("Sequence", tree.Nodes[0].Name); Assert.Equal(1u, tree.Nodes[0].ConditionCount);
        Assert.Equal(1u, tree.Nodes[0].ChildCount); Assert.Equal(0u, tree.Nodes[0].Flags); Assert.Equal(new nuint[] { 1 }, tree.Nodes[0].Children);
        Assert.True(tree.Nodes[1].IsLeaf); Assert.Equal("Travel", tree.Nodes[1].Name); Assert.Null(tree.Nodes[1].Flags);
        Assert.Equal((nuint)15, tree.Nodes[1].OpaqueBegin); Assert.Equal((nuint)16, tree.Nodes[1].OpaqueEnd);
        Assert.Equal("near reference", RadicleEnvironment.PackageLocationTypeName(0)); Assert.Null(RadicleEnvironment.PackageLocationTypeName(15));
        Assert.Equal("alias collection item", RadicleEnvironment.PackageTargetTypeName(8)); Assert.Null(RadicleEnvironment.PackageTargetTypeName(9));
    }

    [Fact]
    public void PackageProcedureFallsBackToOneOpaqueRegionForUnregisteredLeafNames()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Behavior.esm", Build(1, Array.Empty<string>(), Top("PACK", new Rec("PACK", 0x800, new[] {
            ("PKCU", U32(0, 0, 9)), ("ANAM", Z("Procedure")), ("CITC", U32(0)), ("PNAM", Z("Dance")), ("POBA", Array.Empty<byte>()) }))));
        PackageProcedureTree tree;
        using (var env = RadicleEnvironment.Load(new[] { path })) tree = env.PackageProcedure(env.Winner(new FormKey("Behavior.esm", 0x800))!.Value);
        Assert.False(tree.Decoded); Assert.Empty(tree.Inputs); Assert.Empty(tree.Nodes); Assert.Null(tree.NextInputId);
        Assert.Equal((nuint)1, tree.RegionBegin); Assert.Equal((nuint)4, tree.RegionEnd);
    }
}
