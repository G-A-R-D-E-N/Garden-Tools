using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class FixupOrderTests
{
    private static string FixturesRoot => Path.Combine(AppContext.BaseDirectory, "fixtures", "vanilla");

    private static string SkeletonFixture => Path.Combine(FixturesRoot, "Meshes", "Actors", "Character",
                                                          "CharacterAssets", "skeleton.hkx");

    private const string NarrowedSchema = """
    {"classes":{
      "hkpRagdollConstraintData":{"signature":"0x0","size":416,"members":[
        {"name":"atoms","offset":32,"vtype":"TYPE_STRUCT","vsub":"TYPE_VOID","ctype":"hkpRagdollConstraintDataAtoms"}]},
      "hkpRagdollConstraintDataAtoms":{"signature":"0x0","size":384,"members":[
        {"name":"ragdollMotors","offset":160,"vtype":"TYPE_STRUCT","vsub":"TYPE_VOID","ctype":"hkpRagdollMotorConstraintAtom"}]},
      "hkpRagdollMotorConstraintAtom":{"signature":"0x0","size":96,"members":[
        {"name":"motors","offset":64,"vtype":"TYPE_POINTER","vsub":"TYPE_STRUCT","ctype":"hkpConstraintMotor","arrsize":1}]}}}
    """;

    [Fact]
    public void ReorderingTheVanillaSkeletonReproducesEveryTableByteForByte()
    {
        var image = PackfileImage.Read(File.ReadAllBytes(SkeletonFixture));
        var data = image.Section("__data__")!;

        byte[] globals = (byte[])data.GlobalFixups.Clone();
        byte[] locals = (byte[])data.LocalFixups.Clone();
        byte[] virtuals = (byte[])data.VirtualFixups.Clone();

        FixupOrder.Reorder(image);

        Assert.Equal(globals, data.GlobalFixups);
        Assert.Equal(locals, data.LocalFixups);
        Assert.Equal(virtuals, data.VirtualFixups);
    }

    [Fact]
    public void FixedSizePointerArraysAreWalkedSlotBySlot()
    {
        var image = PackfileImage.Read(File.ReadAllBytes(SkeletonFixture));
        var data = image.Section("__data__")!;
        var objects = new PackfileObjects(image);
        var types = HavokClassTypes.Shipped;
        int pointer = objects.PointerWidth;

        int site = MotorSite(objects.OfClass("hkpRagdollConstraintData").First(), types, objects);
        var member = types.Members("hkpRagdollMotorConstraintAtom").Single(m => m.Name == "motors");

        Assert.Equal(3, member.ArrSize);
        Assert.Equal("TYPE_POINTER", member.VType);

        var present = data.Globals().Select(g => g.Source).ToHashSet();
        var order = FixupOrder.Sources(objects, types, data, global: true);
        var rank = new Dictionary<int, int>();
        for (int i = 0; i < order.Count; i++) rank.TryAdd(order[i], i);

        int owners = 0;
        foreach (var owner in objects.OfClass("hkpRagdollConstraintData"))
        {
            int at = MotorSite(owner, types, objects);
            int first = -1;
            for (int slot = 0; slot < member.ArrSize; slot++)
            {
                int source = at + slot * pointer;
                Assert.True(present.Contains(source), $"the fixture holds no global entry at 0x{source:x}");
                Assert.True(rank.ContainsKey(source), $"the walk placed no site at 0x{source:x}");
                if (slot == 0) { first = rank[source]; continue; }
                Assert.Equal(first + slot, rank[source]);
            }
            owners++;
        }

        Assert.Equal(9, owners);
    }

    [Fact]
    public void ADeclaredSiteTheWalkMissesIsRefused()
    {
        var image = Craft("hkpRagdollConstraintData");
        var data = image.Section("__data__")!;
        var objects = new PackfileObjects(image);
        var types = HavokClassTypes.Shipped;
        var owner = objects.Instances.Single();
        var member = types.Members("hkpRagdollMotorConstraintAtom").Single(m => m.Name == "motors");
        int site = MotorSite(owner, types, objects);

        for (int slot = 0; slot < member.ArrSize; slot++)
            data.SetGlobal(site + slot * objects.PointerWidth,
                           image.Sections.IndexOf(data), owner.Offset);

        FixupOrder.Reorder(image);

        var placed = FixupOrder.Sources(new PackfileObjects(image), types, data, global: true);
        for (int slot = 0; slot < member.ArrSize; slot++)
            Assert.Contains(site + slot * objects.PointerWidth, placed);

        var narrowed = HavokClassTypes.Parse(new MemoryStream(Encoding.UTF8.GetBytes(NarrowedSchema)));
        int hidden = site + objects.PointerWidth;

        var error = Assert.Throws<InvalidDataException>(() => FixupOrder.Reorder(image, narrowed));
        Assert.Contains($"0x{hidden:x}", error.Message);
        Assert.Contains("global", error.Message);
    }

    [Fact]
    public void AClassTheSchemaDoesNotKnowIsLeftExactlyAsItWas()
    {
        var image = Craft("hkaSkeleton");
        var data = image.Section("__data__")!;
        var names = image.Section("__classnames__")!;

        data.SetLocal(data.Data.Length - 1, 0);

        int nameAt = NativeAppend.NameOffset(names, "hclClothSetupObject", 0x12345678);
        var virtuals = data.Virtuals().ToList();
        virtuals[0] = (virtuals[0].Source, image.Sections.IndexOf(names), nameAt);
        data.SetVirtuals(virtuals);

        byte[] globals = (byte[])data.GlobalFixups.Clone();
        byte[] locals = (byte[])data.LocalFixups.Clone();

        FixupOrder.Reorder(image);

        Assert.Equal(globals, data.GlobalFixups);
        Assert.Equal(locals, data.LocalFixups);
        Assert.Equal("hclClothSetupObject", new PackfileObjects(image).Instances[0].ClassName);
    }

    private static int MotorSite(PackfileObjects.Instance owner, HavokClassTypes types,
                                 PackfileObjects objects)
    {
        int atoms = types.Members(owner.ClassName)
                         .Single(m => m.CType == "hkpRagdollConstraintDataAtoms").Offset;
        int motors = types.Members("hkpRagdollConstraintDataAtoms")
                          .Single(m => m.CType == "hkpRagdollMotorConstraintAtom").Offset;
        int member = types.Members("hkpRagdollMotorConstraintAtom")
                          .Single(m => m.Name == "motors").Offset;
        return owner.Offset + atoms + motors + member;
    }

    private static PackfileImage Craft(string className)
    {
        var image = new PackfileImage { Predicates = new byte[16] };
        foreach (string name in new[] { "__classnames__", "__types__", "__data__" })
        {
            var tag = new byte[20];
            Encoding.ASCII.GetBytes(name).CopyTo(tag, 0);
            image.Sections.Add(new PackfileSection { TagBytes = tag });
        }

        NativeAppend.Object(image, className);
        return image;
    }
}
