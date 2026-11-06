using System;
using System.Collections.Generic;
using System.Linq;
using Xunit;

namespace Radicle.Interop.Tests;

public class VmadBinderTests
{
    private static readonly VmadOwner Owner = VmadOwner.ForPlacedReference("000800:Quests.esp", "000A00");
    private static readonly VmadOwner Other = VmadOwner.ForPlacedReference("000800:Quests.esp", "000A01");

    [Fact]
    public void ScalarShapesBind()
    {
        var report = VmadBinder.Bind(Owner, new[]
        {
            new VmadShape("Count", "Int", Integer: 7),
            new VmadShape("Scale", "Float", Real: 1.5),
            new VmadShape("Enabled", "Bool", Boolean: true),
            new VmadShape("Label", "String", Text: "hello"),
        });

        Assert.True(report.Clean);
        Assert.Equal(4, report.Bound);
        Assert.Equal(0, report.Unsupported);
        Assert.Empty(report.UnsupportedShapes);
        Assert.Equal(7, report.Properties.Single(property => property.Name == "Count").Value!.Integer);
        Assert.Equal(1.5f, report.Properties.Single(property => property.Name == "Scale").Value!.Real);
        Assert.True(report.Properties.Single(property => property.Name == "Enabled").Value!.Boolean);
        Assert.Equal("hello", report.Properties.Single(property => property.Name == "Label").Value!.Text);
    }

    [Fact]
    public void AnObjectKeepsItsReferenceAndAnAliasBoundObjectIsRefused()
    {
        var report = VmadBinder.Bind(Owner, new[]
        {
            new VmadShape("Target", "Object", Object: "000800:Quests.esp"),
            new VmadShape("Bound", "Object", HasAlias: true, Object: "000800:Quests.esp"),
        });

        var target = report.Properties.Single(property => property.Name == "Target");
        Assert.Equal(VmadBoundKind.Object, target.Kind);
        Assert.Equal("000800:Quests.esp", target.Value!.ObjectReference);
        var aliasBound = report.Properties.Single(property => property.Name == "Bound");
        Assert.Equal(VmadBoundKind.Unsupported, aliasBound.Kind);
        Assert.False(aliasBound.IsBound);
        Assert.Contains("alias", aliasBound.Reason!, StringComparison.Ordinal);
        Assert.Equal(1, report.Bound);
        Assert.Equal(1, report.Unsupported);
        Assert.Single(report.UnsupportedShapes);
    }

    [Fact]
    public void UndecodedArrayAndStructShapesNameTheirCountAndUseThePascalCaseKind()
    {
        var report = VmadBinder.Bind(Owner, new[]
        {
            new VmadShape("Items", "Array", ElementCount: 3),
            new VmadShape("Pair", "Struct", ElementCount: 2),
            new VmadShape("Anything", "Variable"),
            new VmadShape("Odd", "Mystery"),
        });

        Assert.Equal(4, report.Unsupported);
        Assert.Equal(4, report.UnsupportedShapes.Count);
        Assert.Contains("3 elements", string.Join(" ", report.UnsupportedShapes), StringComparison.Ordinal);
        Assert.Contains("2 members", string.Join(" ", report.UnsupportedShapes), StringComparison.Ordinal);
        Assert.All(report.Properties, property => Assert.Equal(VmadBoundKind.Unsupported, property.Kind));
        Assert.StartsWith("Array: ", report.Properties.Single(property => property.Name == "Items").Reason, StringComparison.Ordinal);
        Assert.StartsWith("Struct: ", report.Properties.Single(property => property.Name == "Pair").Reason, StringComparison.Ordinal);
        Assert.StartsWith("Variable: ", report.Properties.Single(property => property.Name == "Anything").Reason, StringComparison.Ordinal);
        Assert.StartsWith("Unknown: ", report.Properties.Single(property => property.Name == "Odd").Reason, StringComparison.Ordinal);
    }

    [Fact]
    public void ArraysAndStructsBindWhenTheirValuesAreCarried()
    {
        var report = VmadBinder.Bind(Owner, new[]
        {
            new VmadShape("Counts", "Array", ElementCount: 2, ElementType: "Int", Elements: new[]
            {
                new VmadShape("", "Int", Integer: 4),
                new VmadShape("", "Int", Integer: 9),
            }),
            new VmadShape("Config", "Struct", ElementCount: 2, Members: new[]
            {
                new VmadShape("Speed", "Int", Integer: 7),
                new VmadShape("Label", "String", Text: "go"),
            }),
            new VmadShape("Empty", "Struct", ElementCount: 0),
        });

        Assert.Equal(2, report.Bound);
        Assert.Equal(1, report.Unsupported);
        var counts = report.Properties.Single(property => property.Name == "Counts");
        Assert.Equal(VmadBoundKind.Array, counts.Kind);
        Assert.Equal(new[] { 4, 9 }, counts.Value!.Elements!.Select(element => element.Integer).ToArray());
        var config = report.Properties.Single(property => property.Name == "Config");
        Assert.Equal(VmadBoundKind.Struct, config.Kind);
        Assert.Equal(7, config.Value!.Members!["Speed"].Integer);
        Assert.Equal("go", config.Value.Members["label"].Text);
        Assert.Contains("0 members", report.Properties.Single(property => property.Name == "Empty").Reason, StringComparison.Ordinal);
    }

    [Fact]
    public void AValueThatCannotBeKeptExactlyIsRefusedInsteadOfTruncated()
    {
        var report = VmadBinder.Bind(Owner, new[]
        {
            new VmadShape("Big", "Int", Integer: 4294967296L),
            new VmadShape("Tenth", "Float", Real: 0.1),
        });

        Assert.Equal(2, report.Unsupported);
        Assert.Equal(0, report.Bound);
        Assert.All(report.Properties, property => Assert.Null(property.Value));
    }

    [Fact]
    public void EveryPropertyStaysOwnerLocalAndTheReportIsDeterministic()
    {
        var shapes = new List<VmadShape>
        {
            new("Zeta", "Array", ElementCount: 1),
            new("Alpha", "Int", Integer: 4),
            new("Beta", "Object", HasAlias: true),
            new("Gamma", "Unknown"),
        };

        var report = VmadBinder.Bind(Owner, shapes);
        var shuffled = VmadBinder.Bind(Owner, Enumerable.Reverse(shapes));
        var other = VmadBinder.Bind(Other, shapes);

        Assert.True(report.Clean);
        Assert.Equal(1, report.Bound);
        Assert.Equal(3, report.Unsupported);
        Assert.Equal(report.Properties.Count, report.Bound + report.Unsupported);
        Assert.Equal(report.Report, shuffled.Report);
        Assert.Equal(report.Digest, shuffled.Digest);
        Assert.NotEqual(report.Digest, other.Digest);
        Assert.All(report.Properties, property => Assert.Equal(report.Properties[0].OwnerKey, property.OwnerKey));
        Assert.NotEqual(report.Properties[0].OwnerKey, other.Properties[0].OwnerKey);
    }

    [Fact]
    public void AShapeNestedPastTheLimitIsRejected()
    {
        var shape = new VmadShape("Leaf", "Int", Integer: 1);
        for (var i = 0; i <= VmadBinder.MaxDepth; ++i) shape = new VmadShape("Nest", "Struct", ElementCount: 1, Members: new[] { shape });

        Assert.Throws<ArgumentException>(() => VmadBinder.Bind(Owner, new[] { shape }));
    }
}
