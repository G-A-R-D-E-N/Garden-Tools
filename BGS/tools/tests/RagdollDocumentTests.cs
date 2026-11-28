using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using OpenCommonwealth.Services.Hkx;
using Xunit;
using Xunit.Abstractions;

namespace BehaviourStudio.Tests;

public sealed class RagdollDocumentTests
{
    private readonly ITestOutputHelper _out;

    public RagdollDocumentTests(ITestOutputHelper output) => _out = output;

    [Fact]
    public void TheHumanFixtureHoldsARagdollThisBuildCanRead()
    {
        var document = new RagdollDocument(Fixture());

        Assert.Single(document.Ragdolls);
        var node = document.Ragdolls[0];
        Assert.Equal(18, node.Bodies.Count);
        Assert.Equal(18, node.Motions.Count);
        Assert.Equal(18, node.Materials.Count);
        Assert.Equal(17, node.Constraints.Count);
        Assert.Equal(18, node.BoneToBodyMap.Count);
        Assert.Equal("Ragdoll_NPC COM", node.Skeleton?.Name);
        Assert.Equal(18, node.Skeleton!.BoneNames.Count);
        Assert.Equal(19, document.Shapes.Count);
        Assert.All(document.Shapes, shape => Assert.Equal("hknpCapsuleShape", shape.ClassName));
        Assert.False(document.Dirty);

        int ragdolls = node.Constraints.Count(row => row.IsRagdoll);
        int hinges = node.Constraints.Count(row => row.ClassName == "hkpLimitedHingeConstraintData");
        _out.WriteLine($"ragdoll #{node.ObjectId}: {node.Bodies.Count} bodies, {node.Motions.Count} motions, " +
                       $"{node.Materials.Count} materials, {ragdolls} ragdoll constraints, {hinges} hinges, " +
                       $"{document.Shapes.Count} capsule shapes");
        Assert.Equal(9, ragdolls);
        Assert.Equal(8, hinges);
    }

    [Fact]
    public void UneditedSerializeReturnsTheSourceBytes()
    {
        byte[] bytes = Fixture();
        var document = new RagdollDocument(bytes);

        Assert.False(document.Dirty);
        Assert.True(document.Serialize().AsSpan().SequenceEqual(bytes));
    }

    [Fact]
    public void ForceRebuildIsByteIdentical()
    {
        byte[] bytes = Fixture();
        var document = new RagdollDocument(bytes);

        byte[] rebuilt = document.ForceRebuild();

        Assert.Equal(bytes.Length, rebuilt.Length);
        Assert.True(rebuilt.AsSpan().SequenceEqual(bytes),
            "an unedited ragdoll put through the whole writer path was not byte-identical");
    }

    [Fact]
    public void EveryFixtureWithARagdollSurvivesAForcedRebuild()
    {
        var files = Directory.GetFiles(FixturesRoot, "*.hkx", SearchOption.AllDirectories)
                             .OrderBy(file => file, StringComparer.Ordinal)
                             .ToList();

        int withRagdoll = 0;
        foreach (string file in files)
        {
            byte[] bytes = File.ReadAllBytes(file);
            if (!CarriesRagdoll(bytes)) continue;

            withRagdoll++;
            var document = new RagdollDocument(bytes);
            Assert.True(document.ForceRebuild().AsSpan().SequenceEqual(bytes),
                $"{Path.GetRelativePath(FixturesRoot, file)} was not byte-identical after a forced rebuild");
        }

        _out.WriteLine($"{withRagdoll} ragdoll fixture(s) of {files.Count} survive an unedited forced rebuild");
        Assert.True(withRagdoll > 0, "no ragdoll fixture was found");
    }

    [Fact]
    public void SetBodyMassWritesOnlyTheMotionRowAndReadsBack()
    {
        byte[] bytes = Fixture();
        var document = new RagdollDocument(bytes);
        var node = document.Ragdolls[0];
        int motion = node.Bodies[0].MotionId;
        float was = node.Motions[motion].Mass;

        document.SetBodyMass(0, 0, was * 3f);
        byte[] produced = document.Serialize();

        Assert.Equal(bytes.Length, produced.Length);
        var objects = Objects(bytes);
        int row = MotionRow(objects, motion);
        AssertConfined(bytes, produced,
            (row + Field("hknpMotionCinfo", "inverseMass"), 4),
            (row + Field("hknpMotionCinfo", "inverseInertiaLocal"), 12));

        var read = new RagdollDocument(produced);
        Assert.Equal(was * 3f, read.Ragdolls[0].Motions[motion].Mass);
    }

    [Fact]
    public void AMassEditScalesTheInverseInertiaByTheMassRatio()
    {
        var document = new RagdollDocument(Fixture());
        var node = document.Ragdolls[0];
        int motion = node.Bodies[0].MotionId;
        float oldMass = node.Motions[motion].Mass;
        var before = node.Motions[motion].InverseInertiaLocal;

        document.SetBodyMass(0, 0, oldMass / 2f);

        float ratio = oldMass / (oldMass / 2f);
        var after = document.Ragdolls[0].Motions[motion].InverseInertiaLocal;
        Assert.Equal(before.X * ratio, after.X);
        Assert.Equal(before.Y * ratio, after.Y);
        Assert.Equal(before.Z * ratio, after.Z);
        Assert.Equal(1f / (oldMass / 2f), document.Ragdolls[0].Motions[motion].InverseMass);
    }

    [Fact]
    public void ACenterOfMassEditStaysInsideTheMotionRowAndReadsBack()
    {
        byte[] bytes = Fixture();
        var document = new RagdollDocument(bytes);
        var node = document.Ragdolls[0];
        int motion = node.Bodies[0].MotionId;
        var wanted = new Vector3(1.25f, -2.5f, 3.75f);
        Assert.NotEqual(wanted, node.Motions[motion].CenterOfMassWorld);

        document.SetBodyCenterOfMass(0, 0, wanted);
        byte[] produced = document.Serialize();

        var objects = Objects(bytes);
        int row = MotionRow(objects, motion);
        AssertConfined(bytes, produced, (row + Field("hknpMotionCinfo", "centerOfMassWorld"), 12));

        Assert.Equal(wanted, new RagdollDocument(produced).Ragdolls[0].Motions[motion].CenterOfMassWorld);
    }

    [Fact]
    public void AFrictionAndRestitutionEditStaysInsideTheMaterialRowAndReadsBack()
    {
        byte[] bytes = Fixture();
        var document = new RagdollDocument(bytes);
        Assert.Equal(1, document.Ragdolls[0].Materials[0].ReferencedByBodies);

        document.SetMaterialFriction(0, 0, 0.5f);
        document.SetMaterialRestitution(0, 0, 0.125f);
        byte[] produced = document.Serialize();

        var objects = Objects(bytes);
        int row = MaterialRow(objects, 0);
        AssertConfined(bytes, produced,
            (row + Field("hknpMaterial", "dynamicFriction"), 4),
            (row + Field("hknpMaterial", "restitution"), 2));

        var read = new RagdollDocument(produced).Ragdolls[0].Materials[0];
        Assert.Equal(0.5f, read.DynamicFriction);
        Assert.Equal(0.5f, read.StaticFriction);
        Assert.Equal(0.125f, read.Restitution);
    }

    [Fact]
    public void AMaterialValueIsRoundedThroughHalfPrecision()
    {
        var document = new RagdollDocument(Fixture());
        document.SetMaterialFriction(0, 0, 0.3f);

        float stored = document.Ragdolls[0].Materials[0].DynamicFriction;
        Assert.Equal((float)(Half)0.3f, stored);
        Assert.Equal(stored, document.Ragdolls[0].Materials[0].StaticFriction);
    }

    [Fact]
    public void ASharedMaterialEditIsRefusedUnlessExplicitlyAllowed()
    {
        var document = new RagdollDocument(Fixture());
        var material = document.Ragdolls[0].Materials[0];
        material.FileSaysShared = true;
        Assert.True(material.Shared);

        var refused = Assert.Throws<InvalidOperationException>(
            () => document.SetMaterialFriction(0, 0, 0.5f));
        Assert.Contains("is shared by", refused.Message);
        Assert.Contains("allowShared", refused.Message);

        document.SetMaterialFriction(0, 0, 0.5f, allowSharedMaterial: true);
        Assert.Equal(0.5f, document.Ragdolls[0].Materials[0].DynamicFriction);

        var counted = new RagdollDocument(Fixture());
        counted.Ragdolls[0].Materials[0].ReferencedByBodies = 2;
        Assert.Contains("shared by 2 body(ies)",
            Assert.Throws<InvalidOperationException>(() => counted.SetMaterialRestitution(0, 0, 0.5f)).Message);
    }


    [Fact]
    public void ARagdollConstraintLimitEditWritesOnlyTheTwist()
    {
        byte[] bytes = Fixture();
        var document = new RagdollDocument(bytes);
        var constraint = document.Ragdolls[0].Constraints.First(row => row.IsRagdoll);
        float wasMinimum = constraint.PlaneMinAngle;
        float wasMaximum = constraint.PlaneMaxAngle;

        document.SetConstraintLimits(0, constraint.Index, -0.5f, 0.5f);
        byte[] produced = document.Serialize();

        var objects = Objects(bytes);
        int at = objects.Instances[constraint.InstanceIndex].Offset;
        int twist = Field("hkpRagdollConstraintData", "atoms") +
                    Field("hkpRagdollConstraintDataAtoms", "twistLimit");
        AssertConfined(bytes, produced,
            (at + twist + Field("hkpTwistLimitConstraintAtom", "minAngle"), 4),
            (at + twist + Field("hkpTwistLimitConstraintAtom", "maxAngle"), 4));

        var read = new RagdollDocument(produced).Ragdolls[0].Constraints[constraint.Index];
        Assert.Equal(-0.5f, read.MinAngle);
        Assert.Equal(0.5f, read.MaxAngle);
        Assert.Equal(wasMinimum, read.PlaneMinAngle);
        Assert.Equal(wasMaximum, read.PlaneMaxAngle);
    }

    [Fact]
    public void ALimitedHingeConstraintLimitEditWritesTheAngularLimit()
    {
        byte[] bytes = Fixture();
        var document = new RagdollDocument(bytes);
        var hinge = document.Ragdolls[0].Constraints
                            .First(row => row.ClassName == "hkpLimitedHingeConstraintData");
        Assert.True(hinge.LimitsEditable);
        Assert.False(hinge.IsRagdoll);

        document.SetConstraintLimits(0, hinge.Index, -1f, 1f);
        byte[] produced = document.Serialize();

        var objects = Objects(bytes);
        int at = objects.Instances[hinge.InstanceIndex].Offset;
        int limit = Field("hkpLimitedHingeConstraintData", "atoms") +
                    Field("hkpLimitedHingeConstraintDataAtoms", "angLimit");
        AssertConfined(bytes, produced,
            (at + limit + Field("hkpAngLimitConstraintAtom", "minAngle"), 4),
            (at + limit + Field("hkpAngLimitConstraintAtom", "maxAngle"), 4));

        var read = new RagdollDocument(produced).Ragdolls[0].Constraints[hinge.Index];
        Assert.Equal(-1f, read.MinAngle);
        Assert.Equal(1f, read.MaxAngle);
    }

    [Fact]
    public void AConstraintFrictionEditReadsBack()
    {
        byte[] bytes = Fixture();
        var document = new RagdollDocument(bytes);
        var constraint = document.Ragdolls[0].Constraints[0];
        float was = constraint.Friction;

        document.SetConstraintFriction(0, 0, was + 1.5f);
        byte[] produced = document.Serialize();

        var objects = Objects(bytes);
        int at = objects.Instances[constraint.InstanceIndex].Offset;
        int friction = Field(constraint.ClassName, "atoms") +
                       Field(constraint.IsRagdoll
                           ? "hkpRagdollConstraintDataAtoms"
                           : "hkpLimitedHingeConstraintDataAtoms", "angFriction") +
                       Field("hkpAngFrictionConstraintAtom", "maxFrictionTorque");
        AssertConfined(bytes, produced, (at + friction, 4));

        Assert.Equal(was + 1.5f, new RagdollDocument(produced).Ragdolls[0].Constraints[0].Friction);
    }

    [Fact]
    public void AConeLimitAndPlaneLimitEditReadBack()
    {
        byte[] bytes = Fixture();
        var document = new RagdollDocument(bytes);
        var constraint = document.Ragdolls[0].Constraints.First(row => row.IsRagdoll);

        document.SetConstraintConeLimit(0, constraint.Index, 0.4f);
        document.SetConstraintPlaneLimits(0, constraint.Index, -0.1f, 0.1f);
        byte[] produced = document.Serialize();

        var read = new RagdollDocument(produced).Ragdolls[0].Constraints[constraint.Index];
        Assert.Equal(0.4f, read.ConeMaxAngle);
        Assert.Equal(-0.1f, read.PlaneMinAngle);
        Assert.Equal(0.1f, read.PlaneMaxAngle);
    }

    [Fact]
    public void AConeOrPlaneLimitOnALimitedHingeIsRefused()
    {
        var document = new RagdollDocument(Fixture());
        var hinge = document.Ragdolls[0].Constraints
                            .First(row => row.ClassName == "hkpLimitedHingeConstraintData");

        Assert.Contains("has no cone limit",
            Assert.Throws<InvalidOperationException>(
                () => document.SetConstraintConeLimit(0, hinge.Index, 0.4f)).Message);
        Assert.Contains("has no plane limit",
            Assert.Throws<InvalidOperationException>(
                () => document.SetConstraintPlaneLimits(0, hinge.Index, -0.1f, 0.1f)).Message);
        Assert.False(document.Dirty);
    }

    [Fact]
    public void ANonFiniteOrNonPositiveValueIsRefused()
    {
        var document = new RagdollDocument(Fixture());
        var node = document.Ragdolls[0];
        int body = node.Bodies[0].Index;

        Assert.Throws<InvalidOperationException>(() => document.SetBodyMass(0, body, 0f));
        Assert.Throws<InvalidOperationException>(() => document.SetBodyMass(0, body, -1f));
        Assert.Throws<InvalidOperationException>(() => document.SetBodyMass(0, body, float.NaN));
        Assert.Throws<InvalidOperationException>(() => document.SetBodyMass(0, body, float.PositiveInfinity));
        Assert.Throws<InvalidOperationException>(
            () => document.SetBodyCenterOfMass(0, body, new Vector3(float.NaN, 0f, 0f)));
        Assert.Throws<InvalidOperationException>(() => document.SetShapeRadius(0, 0f));
        Assert.Throws<InvalidOperationException>(() => document.SetShapeRadius(0, float.NaN));
        Assert.Throws<InvalidOperationException>(
            () => document.SetCapsuleEndpoints(0, new Vector3(0f, 0f, 0f), new Vector3(0f, float.NaN, 0f)));
        Assert.Throws<InvalidOperationException>(() => document.SetMaterialFriction(0, 0, -0.5f));
        Assert.Throws<InvalidOperationException>(() => document.SetMaterialRestitution(0, 0, float.NaN));
        Assert.Throws<InvalidOperationException>(() => document.SetConstraintFriction(0, 0, -1f));
        Assert.False(document.Dirty);
    }

    [Fact]
    public void AnUnorderedOrOutOfRangeLimitIsRefused()
    {
        var document = new RagdollDocument(Fixture());
        var constraint = document.Ragdolls[0].Constraints.First(row => row.IsRagdoll).Index;

        Assert.Throws<InvalidOperationException>(() => document.SetConstraintLimits(0, constraint, 0.5f, -0.5f));
        Assert.Throws<InvalidOperationException>(() => document.SetConstraintLimits(0, constraint, -4f, 0f));
        Assert.Throws<InvalidOperationException>(() => document.SetConstraintLimits(0, constraint, 0f, 4f));
        Assert.Throws<InvalidOperationException>(
            () => document.SetConstraintLimits(0, constraint, float.NaN, 0f));
        Assert.Throws<InvalidOperationException>(() => document.SetConstraintConeLimit(0, constraint, -0.1f));
        Assert.Throws<InvalidOperationException>(() => document.SetConstraintConeLimit(0, constraint, 3.5f));
        Assert.Throws<InvalidOperationException>(() => document.SetConstraintPlaneLimits(0, constraint, 1f, 0f));
        Assert.False(document.Dirty);
    }

    [Fact]
    public void AnIndexOutsideTheModelIsRefused()
    {
        var document = new RagdollDocument(Fixture());
        int ragdolls = document.Ragdolls.Count;
        int bodies = document.Ragdolls[0].Bodies.Count;
        int materials = document.Ragdolls[0].Materials.Count;
        int constraints = document.Ragdolls[0].Constraints.Count;
        int shapes = document.Shapes.Count;

        Assert.Contains($"there is no ragdoll {ragdolls}",
            Assert.Throws<InvalidOperationException>(() => document.SetBodyMass(ragdolls, 0, 1f)).Message);
        Assert.Contains($"there is no body {bodies}",
            Assert.Throws<InvalidOperationException>(() => document.SetBodyMass(0, bodies, 1f)).Message);
        Assert.Contains($"there is no material {materials}",
            Assert.Throws<InvalidOperationException>(() => document.SetMaterialFriction(0, materials, 1f)).Message);
        Assert.Contains($"there is no constraint {constraints}",
            Assert.Throws<InvalidOperationException>(
                () => document.SetConstraintFriction(0, constraints, 1f)).Message);
        Assert.Contains($"there is no shape {shapes}",
            Assert.Throws<InvalidOperationException>(() => document.SetShapeRadius(shapes, 1f)).Message);
        Assert.Throws<InvalidOperationException>(() => document.SetShapeRadius(-1, 1f));
        Assert.False(document.Dirty);
    }

    [Fact]
    public void ABodyWhoseInverseMassIsZeroCannotHaveItsMassSet()
    {
        var document = new RagdollDocument(Fixture());
        int motion = document.Ragdolls[0].Bodies[0].MotionId;
        document.Ragdolls[0].Motions[motion].InverseMass = 0f;

        var refused = Assert.Throws<InvalidOperationException>(() => document.SetBodyMass(0, 0, 5f));
        Assert.Contains("inverse inertia cannot be scaled", refused.Message);
    }

    [Fact]
    public void EveryOtherInstanceInTheFileIsByteIdenticalAfterAnEdit()
    {
        byte[] bytes = Fixture();
        var document = new RagdollDocument(bytes);
        var constraint = document.Ragdolls[0].Constraints.First(row => row.IsRagdoll);
        document.SetConstraintLimits(0, constraint.Index, -0.25f, 0.25f);
        byte[] produced = document.Serialize();

        var objects = Objects(bytes);
        int at = objects.Instances[constraint.InstanceIndex].Offset;
        var allowed = new HashSet<int>();
        int twist = at + Field("hkpRagdollConstraintData", "atoms") +
                    Field("hkpRagdollConstraintDataAtoms", "twistLimit");
        for (int i = 8; i < 16; i++) allowed.Add(twist + i);

        var before = Data(bytes);
        var after = Data(produced);
        var differing = Differing(before, after);
        Assert.NotEmpty(differing);
        Assert.All(differing, offset => Assert.Contains(offset, allowed));

        foreach (var instance in objects.Instances)
        {
            if (instance.Offset == at) continue;
            int? size = HavokClassTypes.Shipped[instance.ClassName]?.Size;
            if (size is not int span || span <= 0) continue;
            for (int i = 0; i < span; i++)
                Assert.Equal(before[instance.Offset + i], after[instance.Offset + i]);
        }

        var was = PackfileImage.Read(bytes);
        var now = PackfileImage.Read(produced);
        var data = was.Section("__data__")!;
        for (int i = 0; i < was.Sections.Count; i++)
        {
            var (left, right) = (was.Sections[i], now.Sections[i]);
            Assert.Equal(left.Tag, right.Tag);
            Assert.Equal(left.HeaderTail, right.HeaderTail);
            Assert.Equal(left.Locals().ToList(), right.Locals().ToList());
            Assert.Equal(left.Globals().ToList(), right.Globals().ToList());
            Assert.Equal(left.Virtuals().ToList(), right.Virtuals().ToList());
            Assert.Equal(left.Exports, right.Exports);
            Assert.Equal(left.Imports, right.Imports);
            if (ReferenceEquals(left, data)) continue;
            Assert.Equal(left.Data, right.Data);
        }
    }

    [Fact]
    public void TheWriterNeverReordersFixups()
    {
        byte[] bytes = Fixture();
        var document = new RagdollDocument(bytes);
        document.SetBodyMass(0, 0, document.Ragdolls[0].Motions[document.Ragdolls[0].Bodies[0].MotionId].Mass * 2f);
        byte[] produced = document.Serialize();

        var was = PackfileImage.Read(bytes);
        var now = PackfileImage.Read(produced);
        Assert.Equal(was.Sections.Count, now.Sections.Count);

        for (int i = 0; i < was.Sections.Count; i++)
        {
            Assert.Equal(was.Sections[i].Locals().ToList(), now.Sections[i].Locals().ToList());
            Assert.Equal(was.Sections[i].Globals().ToList(), now.Sections[i].Globals().ToList());
            Assert.Equal(was.Sections[i].Virtuals().ToList(), now.Sections[i].Virtuals().ToList());
        }
    }

    [Fact]
    public void AForcedRebuildCannotCarryEdits()
    {
        byte[] bytes = Fixture();
        var document = new RagdollDocument(bytes);
        document.SetBodyMass(0, 0, document.Ragdolls[0].Motions[document.Ragdolls[0].Bodies[0].MotionId].Mass * 2f);

        Assert.Contains("a forced no-op rebuild cannot carry edits",
            Assert.Throws<InvalidOperationException>(
                () => RagdollWriteback.Write(bytes, document.Ragdolls, document.Shapes,
                                             document.RagdollChanges(), document.ShapeChanges(), force: true)).Message);
    }

    [Fact]
    public void AChangeOfRowCountIsRefused()
    {
        byte[] bytes = Fixture();
        var document = new RagdollDocument(bytes);
        var node = document.Ragdolls[0];
        var doctored = node.Copy();
        doctored.Bodies = node.Bodies.Take(node.Bodies.Count - 1).ToList();

        Assert.Contains("rows are never added or removed",
            Assert.Throws<InvalidDataException>(
                () => RagdollWriteback.Write(bytes, document.Ragdolls, document.Shapes,
                                             new[] { new RagdollChange(doctored, node) },
                                             Array.Empty<RagdollShapeChange>())).Message);
    }

    [Fact]
    public void AFileWithNoRagdollIsRefused()
    {
        string path = Path.Combine(FixturesRoot, "Meshes", "Actors", "Character", "CharacterAssets", "Hair",
                                   "Female", "FemaleHair04.hkx");
        var refused = Assert.Throws<InvalidDataException>(() => new RagdollDocument(File.ReadAllBytes(path)));
        Assert.Contains("no hknpRagdollData", refused.Message);
    }

    [Fact]
    public void ASavedDocumentReadsBackAndKeepsABackup()
    {
        string path = Path.Combine(Path.GetTempPath(), $"bgs-ragdoll-{Guid.NewGuid():N}.hkx");
        byte[] bytes = Fixture();
        File.Copy(FixturePath(), path);
        try
        {
            var document = RagdollDocument.Open(path);
            float was = document.Ragdolls[0].Motions[document.Ragdolls[0].Bodies[0].MotionId].Mass;
            document.SetBodyMass(0, 0, was * 4f);
            Assert.True(document.Dirty);

            document.Save();

            Assert.False(document.Dirty);
            Assert.True(File.ReadAllBytes(path + ".bak").AsSpan().SequenceEqual(bytes),
                "the backup did not preserve the original");
            var reopened = new RagdollDocument(File.ReadAllBytes(path));
            Assert.Equal(was * 4f, reopened.Ragdolls[0].Motions[reopened.Ragdolls[0].Bodies[0].MotionId].Mass);
        }
        finally
        {
            File.Delete(path);
            File.Delete(path + ".bak");
        }
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

    private static int MemberRow(PackfileObjects objects, string member, string elementClass, int index)
    {
        var ragdoll = objects.Instances.First(instance => instance.ClassName == "hknpRagdollData");
        int header = objects.FieldAt(ragdoll, member)!.Value;
        int stride = HavokClassTypes.Shipped[elementClass]!.Size!.Value;
        return objects.ArrayAt(header, stride)!.At + index * stride;
    }

    private static int MotionRow(PackfileObjects objects, int motion) =>
        MemberRow(objects, "motionCinfos", "hknpMotionCinfo", motion);

    private static int MaterialRow(PackfileObjects objects, int material) =>
        MemberRow(objects, "materials", "hknpMaterial", material);

    private static List<int> Differing(byte[] before, byte[] after)
    {
        Assert.Equal(before.Length, after.Length);
        var offsets = new List<int>();
        for (int i = 0; i < before.Length; i++)
            if (before[i] != after[i]) offsets.Add(i);
        return offsets;
    }

    private static void AssertConfined(byte[] beforeFile, byte[] afterFile, params (int At, int Length)[] allowed)
    {
        byte[] before = Data(beforeFile);
        byte[] after = Data(afterFile);
        Assert.Equal(before.Length, after.Length);

        var differing = Differing(before, after);
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
