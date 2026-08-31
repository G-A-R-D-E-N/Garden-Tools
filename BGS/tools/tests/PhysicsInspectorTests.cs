using System;
using System.IO;
using System.Linq;
using System.Xml;
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
    public void VanillaCharacterSkeletonPhysicsObjectUsesReadOnlyInspectorFields()
    {
        var obj = FindCataloguedObject(
            "Meshes/Actors/Character/CharacterAssets/skeleton.hkx",
            family => family != HavokPhysicsClassFamily.Cloth);

        Assert.True(Inspector.IsSchemaReadOnlyClass(obj.Class));
        Assert.Equal(obj.Class,
            Inspector.ReadOnlyClassFromHeading($"#{obj.Id}   {obj.Class}   4 editable fields"));
        AssertObjectHasVisibleValue(obj);
    }

    [Fact]
    public void VanillaHairClothObjectUsesReadOnlyInspectorFields()
    {
        var obj = FindCataloguedObject(
            "Meshes/Actors/Character/CharacterAssets/Hair/Female/FemaleHair04.hkx",
            family => family == HavokPhysicsClassFamily.Cloth);

        Assert.True(Inspector.IsSchemaReadOnlyClass(obj.Class));
        AssertObjectHasVisibleValue(obj);
    }

    [Fact]
    public void NormalBehaviourClassesRemainEditable()
    {
        Assert.False(Inspector.IsSchemaReadOnlyClass("hkbClipGenerator"));
    }

    private static GraphObject FindCataloguedObject(
        string relative,
        Func<HavokPhysicsClassFamily, bool> predicate)
    {
        byte[] bytes = File.ReadAllBytes(Fixture(relative));
        var model = BehaviourGraphModel.Parse(NativeXml.From(bytes));
        var found = model.Objects.FirstOrDefault(obj =>
        {
            if (!HavokPhysicsSchemaCatalog.TryGetSignature(obj.Class, out _)) return false;
            var family = HavokPhysicsSchemaCatalog.FamilyOf(obj.Class);
            return family.HasValue && predicate(family.Value);
        });

        Assert.NotNull(found);
        return found!;
    }

    private static void AssertObjectHasVisibleValue(GraphObject obj)
    {
        var parameter = obj.Node.SelectNodes("hkparam")?
            .Cast<XmlNode>()
            .FirstOrDefault(node => (node.Attributes?["name"]?.Value ?? "").Length > 0);

        Assert.NotNull(parameter);
        string name = parameter!.Attributes!["name"]!.Value;
        Assert.Equal(parameter.InnerText.Trim(), obj.Str(name));
    }
}
