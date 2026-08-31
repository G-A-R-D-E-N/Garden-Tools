using System;
using System.IO;
using System.Linq;
using System.Xml;
using BehaviourStudio.App;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class PhysicsInspectorTests
{
    private static string Fixture(string relative) =>
        Path.Combine(AppContext.BaseDirectory, "fixtures/vanilla",
                     relative.Replace('/', Path.DirectorySeparatorChar));

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
