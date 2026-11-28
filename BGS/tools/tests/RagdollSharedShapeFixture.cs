using System;
using System.Collections.Generic;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

internal static class RagdollSharedShapeFixture
{
    internal static byte[] ShareProperties(byte[] bytes, out int source, out int target)
    {
        var before = new RagdollDocument(bytes);
        var image = PackfileImage.Read(bytes);
        var objects = new PackfileObjects(image);
        var section = image.Section(PackfileObjects.DataSection) ??
                      throw new InvalidOperationException("the fixture has no data section");

        var choices = new List<(int Instance, int Field, int Container)>();
        for (int i = 0; i < objects.Instances.Count; i++)
        {
            var instance = objects.Instances[i];
            if (instance.ClassName != "hknpCapsuleShape") continue;
            if (objects.FieldAt(instance, "properties") is not int field) continue;
            if (RagdollCapsule.MassPropertiesAt(objects, instance) is null) continue;

            var container = objects.ReadRefAt(field, out bool wasNull);
            if (wasNull || container == null) continue;
            choices.Add((i, field, container.Offset));
        }

        bool found = false;
        int sourceInstance = -1, targetInstance = -1, targetField = -1, containerAt = -1;
        for (int i = 0; i < choices.Count && !found; i++)
        {
            int index = ShapeAt(before, choices[i].Instance);
            if (before.Shapes[index].ReferencedByBodies > 1) continue;

            for (int j = 0; j < choices.Count && !found; j++)
            {
                if (i == j || choices[i].Container == choices[j].Container) continue;

                sourceInstance = choices[i].Instance;
                targetInstance = choices[j].Instance;
                targetField = choices[j].Field;
                containerAt = choices[i].Container;
                found = true;
            }
        }

        Assert.True(found, "the fixture does not hold two capsule shapes with their own mass properties");

        bool patched = false;
        for (int at = 0; at + 8 <= section.LocalFixups.Length && !patched; at += 8)
        {
            if (BitConverter.ToInt32(section.LocalFixups, at) != targetField) continue;
            BitConverter.GetBytes(containerAt).CopyTo(section.LocalFixups, at + 4);
            patched = true;
        }

        int self = image.Sections.IndexOf(section);
        for (int at = 0; at + 12 <= section.GlobalFixups.Length && !patched; at += 12)
        {
            if (BitConverter.ToInt32(section.GlobalFixups, at) != targetField) continue;
            if (BitConverter.ToInt32(section.GlobalFixups, at + 4) != self) continue;
            BitConverter.GetBytes(containerAt).CopyTo(section.GlobalFixups, at + 8);
            patched = true;
        }

        Assert.True(patched, $"the fixture has no fixup for the properties field at 0x{targetField:x}");
        source = sourceInstance;
        target = targetInstance;
        return image.Rebuild();
    }

    internal static int ShapeAt(RagdollDocument document, int instanceIndex)
    {
        for (int i = 0; i < document.Shapes.Count; i++)
            if (document.Shapes[i].InstanceIndex == instanceIndex) return i;

        throw new InvalidOperationException($"the document holds no shape with instance index {instanceIndex}");
    }
}