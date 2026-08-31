using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Avalonia.Controls;
using BehaviourStudio.App;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class PhysicsInspectorTests
{
    private static string Fixture(string relative) =>
        Path.Combine(AppContext.BaseDirectory, "fixtures/vanilla",
                     relative.Replace('/', Path.DirectorySeparatorChar));

    [Theory]
    [InlineData("hkpRagdollConstraintData")]
    [InlineData("hclClothData")]
    public void CataloguedSchemaRowsAreActuallyReadOnly(string className)
    {
        var inspector = new Inspector(300);
        var heading = new TextBlock { Text = $"#12   {className}   1 editable fields" };
        inspector.Add(heading);

        var value = new TextBox { Text = "42" };
        inspector.TwoColumnRow(new TextBlock { Text = "value" }, value);

        Assert.True(inspector.SchemaReadOnly);
        Assert.True(value.IsReadOnly);
        Assert.False(value.IsTabStop);
        Assert.Contains("read-only fields", heading.Text);
    }

    [Fact]
    public void UnknownPhysicsLookingClassDoesNotBecomeReadOnly()
    {
        Assert.False(HavokPhysicsSchemaCatalog.TryGetSignature("hkpUnknownExperimentalThing", out _));
        Assert.False(Inspector.IsSchemaReadOnlyClass("hkpUnknownExperimentalThing"));
    }

    [Fact]
    public void VanillaCharacterSkeletonPhysicsObjectIsReachableAndSchemaRendered()
    {
        AssertFixtureObject(
            "Meshes/Actors/Character/CharacterAssets/skeleton.hkx",
            family => family != HavokPhysicsClassFamily.Cloth);
    }

    [Fact]
    public void VanillaHairClothObjectIsReachableAndSchemaRendered()
    {
        AssertFixtureObject(
            "Meshes/Actors/Character/CharacterAssets/Hair/Female/FemaleHair04.hkx",
            family => family == HavokPhysicsClassFamily.Cloth);
    }

    [Fact]
    public void NormalBehaviourClassesRemainEditable()
    {
        Assert.False(Inspector.IsSchemaReadOnlyClass("hkbClipGenerator"));
    }

    private static void AssertFixtureObject(
        string relative,
        Func<HavokPhysicsClassFamily, bool> predicate)
    {
        string fixture = Fixture(relative);
        byte[] bytes = File.ReadAllBytes(fixture);
        var image = PackfileImage.Read(bytes);
        var objects = new PackfileObjects(image);

        var instance = objects.Instances.FirstOrDefault(candidate =>
        {
            if (!HavokPhysicsSchemaCatalog.TryGetSignature(candidate.ClassName, out _)) return false;
            var family = HavokPhysicsSchemaCatalog.FamilyOf(candidate.ClassName);
            return family.HasValue && predicate(family.Value);
        });

        Assert.NotNull(instance);
        Assert.True(Inspector.IsSchemaReadOnlyClass(instance!.ClassName));

        int index = objects.IndexOf(instance);
        Assert.True(index >= 0);
        string id = (NativeGraphModel.FirstId + index).ToString();
        string xml = NativeXml.From(objects, image);
        var model = BehaviourGraphModel.Parse(xml);
        var obj = model.Get(id);
        Assert.NotNull(obj);
        Assert.Equal(instance.ClassName, obj!.Class);
        Assert.Equal(instance.ClassName,
            Inspector.ReadOnlyClassFromHeading($"#{id}   {instance.ClassName}   4 editable fields"));

        var root = HkxBehaviorParser.ParseBehavior(fixture);
        Assert.NotNull(root);
        Assert.Contains(Reachable(root!), node =>
            node.Offset == instance.Offset && node.ClassName == instance.ClassName);

        var parameters = HkxTextEdit.ReadParams(xml, id);
        Assert.NotEmpty(parameters);
        var plain = parameters.Select(parameter => (parameter.Name, parameter.Value)).ToList();

        string Reference(PackfileObjects.Instance? target, bool wasNull)
        {
            if (wasNull) return "null";
            if (target == null) return "";
            int at = objects.IndexOf(target);
            return at < 0 ? "" : "#" + (NativeGraphModel.FirstId + at);
        }

        var fields = PanelFields.For(objects, instance, plain, Reference);
        Assert.Equal(parameters.Count, fields.Count);
        Assert.Contains(fields, field => field.From == PanelFields.Source.Bytes);
        Assert.Contains(fields, field =>
            field.From == PanelFields.Source.Bytes && field.Value.Length > 0 &&
            parameters.Any(parameter => parameter.Name == field.Name && parameter.Value == field.Value));
    }

    private static IEnumerable<HkxBehaviorParser.BehaviorNode> Reachable(
        HkxBehaviorParser.BehaviorNode root)
    {
        var seen = new HashSet<int>();
        var pending = new Stack<HkxBehaviorParser.BehaviorNode>();
        pending.Push(root);

        while (pending.Count > 0)
        {
            var node = pending.Pop();
            if (!seen.Add(node.Offset)) continue;
            yield return node;
            for (int i = node.Children.Count - 1; i >= 0; i--) pending.Push(node.Children[i]);
        }
    }
}
