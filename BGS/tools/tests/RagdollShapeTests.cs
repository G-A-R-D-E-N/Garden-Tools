using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using OpenCommonwealth.Services.Hkx;
using Xunit;
using Xunit.Abstractions;

namespace BehaviourStudio.Tests;

public sealed class RagdollShapeTests
{
    private readonly ITestOutputHelper _out;

    public RagdollShapeTests(ITestOutputHelper output) => _out = output;

    [Fact]
    public void ACapsuleEditDerivesThePolytopeAndReadsBack()
    {
        var document = new RagdollDocument(Fixture());
        var shape = document.Shapes[0];
        Assert.True(shape.CarriesPolytopeData);
        Assert.True(shape.Vertices > 0 && shape.Planes > 0 && shape.Faces > 0 && shape.Indices > 0);
        byte[] was = shape.Capsule!.Vertices;
        float radius = shape.Radius;

        document.SetShapeRadius(0, radius * 1.3f, true);
        Assert.True(document.Dirty);

        var read = new RagdollDocument(document.Serialize()).Shapes[0];
        Assert.Equal(radius * 1.3f, read.Radius);
        Assert.NotEqual(was, read.Capsule!.Vertices);
        Assert.True(document.Shapes[0].SameAs(read));
    }

    [Fact]
    public void EveryCapsuleInEveryFixtureDerivesItsPolytope()
    {
        int shapes = 0;
        foreach (string file in Directory.GetFiles(FixturesRoot, "*.hkx", SearchOption.AllDirectories)
                                         .OrderBy(file => file, StringComparer.Ordinal))
        {
            byte[] bytes = File.ReadAllBytes(file);
            if (!CarriesRagdoll(bytes)) continue;

            var document = new RagdollDocument(bytes);
            for (int i = 0; i < document.Shapes.Count; i++)
            {
                shapes++;
                var shape = document.Shapes[i];
                Assert.True(shape.CarriesPolytopeData, $"{Path.GetFileName(file)} shape {i} carries no polytope array");
                Assert.True(shape.Capsule is not null, $"{Path.GetFileName(file)} shape {i} carries no payload");

                document.SetCapsuleEndpoints(i, shape.A, shape.A + (shape.B - shape.A) * 1.05f, true);
                Assert.True(document.Dirty);
            }

            var read = new RagdollDocument(document.Serialize());
            for (int i = 0; i < read.Shapes.Count; i++)
                Assert.True(document.Shapes[i].SameAs(read.Shapes[i]));
        }

        _out.WriteLine($"{shapes} capsule shape(s) across the fixtures derive a new polytope and read back");
        Assert.True(shapes > 0, "no capsule shape was found in the fixtures");
    }

    [Fact]
    public void AWriterCannotWriteACapsuleGeometryEditThatSkipsTheDerivation()
    {
        byte[] bytes = Fixture();
        var document = new RagdollDocument(bytes);
        var original = document.Shapes[0];
        var edited = original.Copy();
        edited.Radius = original.Radius + 1f;

        Assert.Contains("does not produce", Assert.Throws<InvalidOperationException>(
            () => RagdollWriteback.Write(bytes, document.Ragdolls, document.Shapes,
                                         Array.Empty<RagdollChange>(),
                                         new[] { new RagdollShapeChange(edited, original) })).Message);
    }

    [Fact]
    public void AChangedDerivedArrayCountIsNotTheSameShape()
    {
        var document = new RagdollDocument(Fixture());
        var shape = document.Shapes[0];
        var copy = shape.Copy();
        Assert.True(shape.SameAs(copy));

        copy.Vertices = shape.Vertices + 1;
        Assert.False(shape.SameAs(copy));
    }

    [Fact]
    public void TheFixtureUsesTheVerifiedConeAndPlaneAngleMeasurementModes()
    {
        var document = new RagdollDocument(Fixture());
        var rows = document.Ragdolls.SelectMany(node => node.Constraints).Where(row => row.IsRagdoll).ToList();
        Assert.NotEmpty(rows);
        Assert.All(rows, row => Assert.Equal(0, row.ConeAngleMeasurementMode));
        Assert.All(rows, row => Assert.Equal(1, row.PlaneAngleMeasurementMode));
        Assert.All(rows, row => Assert.True(row.ConeEditable && row.PlaneEditable));
        Assert.All(rows, row => Assert.Equal(-100f, row.ConeMinAngle));
        _out.WriteLine($"{rows.Count} ragdoll constraint(s) use cone mode 0 and plane mode 1");
    }

    [Fact]
    public void AConeEditIsRefusedWhenTheAtomUsesAnotherMeasurementMode()
    {
        var document = new RagdollDocument(Fixture());
        var constraint = document.Ragdolls[0].Constraints.First(row => row.IsRagdoll);
        constraint.ConeAngleMeasurementMode = 3;

        var refused = Assert.Throws<InvalidOperationException>(
            () => document.SetConstraintConeLimit(0, constraint.Index, 0.4f));
        Assert.Contains("angle measurement mode 3", refused.Message);
        Assert.False(document.Dirty);

        byte[] bytes = Fixture();
        var doctored = new RagdollDocument(bytes);
        var node = doctored.Ragdolls[0].Copy();
        var constraints = node.Constraints.ToList();
        int at = constraints.FindIndex(row => row.IsRagdoll);
        var edit = constraints[at].Copy();
        edit.ConeAngleMeasurementMode = 3;
        edit.ConeMaxAngle = 0.4f;
        constraints[at] = edit;
        node.Constraints = constraints;

        Assert.Contains("angle measurement mode 3", Assert.Throws<InvalidOperationException>(
            () => RagdollWriteback.Write(bytes, doctored.Ragdolls, doctored.Shapes,
                                         new[] { new RagdollChange(node, doctored.Ragdolls[0]) },
                                         Array.Empty<RagdollShapeChange>())).Message);
    }

    [Fact]
    public void APlaneEditIsRefusedWhenTheAtomUsesAnotherMeasurementMode()
    {
        var document = new RagdollDocument(Fixture());
        var constraint = document.Ragdolls[0].Constraints.First(row => row.IsRagdoll);
        constraint.PlaneAngleMeasurementMode = 0;

        var refused = Assert.Throws<InvalidOperationException>(
            () => document.SetConstraintPlaneLimits(0, constraint.Index, -0.1f, 0.1f));
        Assert.Contains("angle measurement mode 0", refused.Message);
        Assert.False(document.Dirty);
    }

    [Fact]
    public void TheTwoFrictionLanesAreWrittenSeparatelyAndOnlyWhenChanged()
    {
        byte[] bytes = Fixture();
        var document = new RagdollDocument(bytes);
        float dynamic = document.Ragdolls[0].Materials[0].DynamicFriction;
        float wanted = dynamic == 0.25f ? 0.5f : 0.25f;

        document.SetMaterialFriction(0, 0, dynamic, wanted);
        byte[] produced = document.Serialize();

        int row = MaterialRow(Objects(bytes), 0);
        AssertConfined(bytes, produced, (row + Field("hknpMaterial", "staticFriction"), 2));

        var read = new RagdollDocument(produced).Ragdolls[0].Materials[0];
        Assert.Equal(dynamic, read.DynamicFriction);
        Assert.Equal(wanted, read.StaticFriction);
    }

    [Fact]
    public void SeparateFrictionValuesAreStoredAtHalfPrecision()
    {
        var document = new RagdollDocument(Fixture());
        document.SetMaterialFriction(0, 0, 0.3f, 0.7f);

        var material = document.Ragdolls[0].Materials[0];
        Assert.Equal((float)(Half)0.3f, material.DynamicFriction);
        Assert.Equal((float)(Half)0.7f, material.StaticFriction);
        Assert.NotEqual(material.DynamicFriction, material.StaticFriction);
    }

    [Fact]
    public void SetBodyMassesValidatesEveryRowBeforeItChangesAnything()
    {
        var document = new RagdollDocument(Fixture());
        var node = document.Ragdolls[0];
        int motion = node.Bodies[0].MotionId;
        float was = node.Motions[motion].InverseMass;

        var refused = Assert.Throws<InvalidOperationException>(() => document.SetBodyMasses(0,
            new[] { (0, node.Motions[motion].Mass * 2f), (1, -5f) }));
        Assert.Contains("is not a positive finite number", refused.Message);
        Assert.Equal(was, document.Ragdolls[0].Motions[motion].InverseMass);
        Assert.False(document.Dirty);
    }

    [Fact]
    public void SetBodyMassesReportsRowsTheRagdollDoesNotHold()
    {
        var document = new RagdollDocument(Fixture());
        var node = document.Ragdolls[0];
        int motion = node.Bodies[0].MotionId;
        float wanted = node.Motions[motion].Mass * 3f;

        var outcome = document.SetBodyMasses(0, new[] { (0, wanted), (999, wanted) });

        Assert.Equal(new[] { 0 }, outcome.Applied);
        Assert.Equal(new[] { 999 }, outcome.Skipped);
        Assert.Equal(wanted, new RagdollDocument(document.Serialize()).Ragdolls[0].Motions[motion].Mass);
    }

    [Fact]
    public void SetConstraintLimitEditsAreAllOrNothingAndReportSkippedRows()
    {
        var document = new RagdollDocument(Fixture());
        var node = document.Ragdolls[0];
        int first = node.Constraints.First(row => row.LimitsEditable).Index;
        int hinge = node.Constraints.First(row => row.ClassName == "hkpLimitedHingeConstraintData").Index;

        Assert.Throws<InvalidOperationException>(() => document.SetConstraintLimits(0, new[]
        {
            new RagdollLimitEdit(first, -0.25f, 0.25f, null),
            new RagdollLimitEdit(hinge, 1f, -1f, null),
        }));
        Assert.False(document.Dirty);

        var outcome = document.SetConstraintLimits(0, new[]
        {
            new RagdollLimitEdit(first, -0.25f, 0.25f, null),
            new RagdollLimitEdit(999, -0.25f, 0.25f, null),
        });
        Assert.Equal(new[] { first }, outcome.Applied);
        Assert.Equal(new[] { 999 }, outcome.Skipped);

        var read = new RagdollDocument(document.Serialize()).Ragdolls[0].Constraints[first];
        Assert.Equal(-0.25f, read.MinAngle);
        Assert.Equal(0.25f, read.MaxAngle);
    }

    [Fact]
    public void SetConstraintLimitEditsSkipARagdollWhoseConeModeIsNotVerified()
    {
        var document = new RagdollDocument(Fixture());
        var constraint = document.Ragdolls[0].Constraints.First(row => row.IsRagdoll);
        constraint.ConeAngleMeasurementMode = 4;

        var outcome = document.SetConstraintLimits(0, new[]
        {
            new RagdollLimitEdit(constraint.Index, -0.25f, 0.25f, 0.5f),
        });

        Assert.Empty(outcome.Applied);
        Assert.Equal(new[] { constraint.Index }, outcome.Skipped);
        Assert.False(document.Dirty);
    }

    private static byte[] Fixture() => File.ReadAllBytes(FixturePath());

    private static string FixturePath() => Path.Combine(
        FixturesRoot, "Meshes", "Actors", "Character", "CharacterAssets", "skeleton.hkx");

    private static string FixturesRoot => Path.Combine(AppContext.BaseDirectory, "fixtures", "vanilla");

    private static PackfileObjects Objects(byte[] bytes) => new(PackfileImage.Read(bytes));

    private static bool CarriesRagdoll(byte[] bytes)
    {
        try { return Objects(bytes).Instances.Any(instance => instance.ClassName == "hknpRagdollData"); }
        catch (Exception) { return false; }
    }

    private static int Field(string className, string member) =>
        HavokClasses.Shipped.Field(className, member)!.Offset;

    private static int MaterialRow(PackfileObjects objects, int material)
    {
        var ragdoll = objects.Instances.First(instance => instance.ClassName == "hknpRagdollData");
        int header = objects.FieldAt(ragdoll, "materials")!.Value;
        int stride = HavokClassTypes.Shipped["hknpMaterial"]!.Size!.Value;
        return objects.ArrayAt(header, stride)!.At + material * stride;
    }

    private static void AssertConfined(byte[] beforeFile, byte[] afterFile, params (int At, int Length)[] allowed)
    {
        byte[] before = Data(beforeFile);
        byte[] after = Data(afterFile);
        Assert.Equal(before.Length, after.Length);

        var differing = new List<int>();
        for (int i = 0; i < before.Length; i++)
            if (before[i] != after[i]) differing.Add(i);

        Assert.NotEmpty(differing);
        foreach (int offset in differing)
        {
            bool inside = allowed.Any(range => offset >= range.At && offset < range.At + range.Length);
            Assert.True(inside, $"byte 0x{offset:x} of __data__ changed outside every field the edit owns (allowed " +
                                $"{string.Join(", ", allowed.Select(range => $"0x{range.At:x}+{range.Length}"))})");
        }
    }

    private static byte[] Data(byte[] file) =>
        PackfileImage.Read(file).Section("__data__")!.Data;
}
