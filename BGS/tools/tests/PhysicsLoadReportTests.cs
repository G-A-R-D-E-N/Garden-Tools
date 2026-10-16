using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class PhysicsLoadReportTests
{
    private static string Fixture(string relative) =>
        Path.Combine(AppContext.BaseDirectory, "fixtures/vanilla",
                     relative.Replace('/', Path.DirectorySeparatorChar));

    private const string Skeleton = "Meshes/Actors/Character/CharacterAssets/skeleton.hkx";
    private const string ClothHair = "Meshes/Actors/Character/CharacterAssets/Hair/Female/FemaleHair04.hkx";

    [Fact]
    public void VanillaSkeletonIsReportedOk()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(Fixture(Skeleton)), out var report);

        Assert.NotNull(model);
        Assert.Equal(PhysicsLoadStatus.Ok, report.Status);
        Assert.Equal(PhysicsLoadSeverity.Info, report.Severity);
        Assert.Null(report.FailingStage);
        Assert.Empty(report.UnsupportedShapeClasses);
    }

    [Fact]
    public void VanillaSkeletonReportCarriesTheMeasuredCensus()
    {
        HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(Fixture(Skeleton)), out var report);

        Assert.Equal(11, report.FileVersion);
        Assert.Equal(8, report.PointerSize);
        Assert.Equal(19, report.ClassCounts["hknpCapsuleShape"]);
        Assert.Equal(1, report.CountOf(PhysicsLoadReport.RagdollClass));
        Assert.Equal(19, report.ShapeClasses["hknpCapsuleShape"]);
        Assert.Equal(9, report.ConstraintClasses["hkpRagdollConstraintData"]);
        Assert.Equal(8, report.ConstraintClasses["hkpLimitedHingeConstraintData"]);
        Assert.True(report.MapperPresent);
        Assert.False(report.MaterialPresent);
        Assert.Equal(18, report.BodyCount);
        Assert.Equal(18, report.UsableBodies);
        Assert.Equal(0, report.SkippedBodies);
        Assert.Equal(0, report.SkippedConstraints);
        Assert.Equal(0, report.UnsupportedShapeBodies);
        Assert.Equal(18, report.Mappings);
        Assert.Equal(19, report.ShapeCount);
        Assert.Equal(17, report.ConstraintCount);
    }

    [Fact]
    public void AClassListWithNoPhysicsAtAllIsSkeletonOnly()
    {
        var report = PhysicsLoadClassifier.Classify(new PhysicsLoadFacts
        {
            ClassCounts = new Dictionary<string, int> { ["hkaSkeleton"] = 1, ["hkaAnimationContainer"] = 1 },
        });

        Assert.Equal(PhysicsLoadStatus.SkeletonOnly, report.Status);
        Assert.Equal(PhysicsLoadSeverity.Warning, report.Severity);
        Assert.Contains("has a skeleton but no ragdoll", report.Headline);
        Assert.Contains("hknpRagdollData", report.Headline);
        Assert.Contains("re-export with physics included", report.Summary);
    }

    [Fact]
    public void AFileWithNeitherSkeletonNorPhysicsSaysSo()
    {
        var report = PhysicsLoadClassifier.Classify(new PhysicsLoadFacts
        {
            ClassCounts = new Dictionary<string, int> { ["hkRootLevelContainer"] = 1 },
        });

        Assert.Equal(PhysicsLoadStatus.SkeletonOnly, report.Status);
        Assert.Equal(PhysicsLoadSeverity.Info, report.Severity);
        Assert.Contains("no skeleton", report.Headline);
        Assert.Contains("hknpRagdollData", report.Headline);
    }

    [Fact]
    public void LegacyHkpBodiesAreReportedAsLegacyPhysics()
    {
        var report = PhysicsLoadClassifier.Classify(new PhysicsLoadFacts
        {
            FileVersion = 11,
            PointerSize = 8,
            ClassCounts = new Dictionary<string, int>
            {
                ["hkpRigidBody"] = 12,
                ["hkpSphereShape"] = 12,
                ["hkpPhysicsSystem"] = 1,
                ["hkpRagdollConstraintData"] = 11,
                ["hkaSkeleton"] = 1,
            },
        });

        Assert.Equal(PhysicsLoadStatus.LegacyPhysics, report.Status);
        Assert.Equal(PhysicsLoadSeverity.Error, report.Severity);
        Assert.Contains("older hkp format", report.Headline);
        Assert.Contains("hkpRigidBody x12", report.Headline);
        Assert.Contains("hkpPhysicsSystem x1", report.Headline);
        Assert.Contains("hknpRagdollData", report.Headline);
    }

    [Fact]
    public void LegacyConstraintDataAloneNeverSelectsLegacyPhysics()
    {
        var report = PhysicsLoadClassifier.Classify(new PhysicsLoadFacts
        {
            FileVersion = 11,
            PointerSize = 8,
            ClassCounts = new Dictionary<string, int>
            {
                ["hknpRagdollData"] = 1,
                ["hknpCapsuleShape"] = 18,
                ["hknpPhysicsSceneData"] = 1,
                ["hkaSkeletonMapper"] = 2,
                ["hkpRagdollConstraintData"] = 9,
                ["hkpLimitedHingeConstraintData"] = 8,
                ["hkpPositionConstraintMotor"] = 1,
            },
            BodyCount = 18,
            UsableBodies = 18,
            ShapeCount = 18,
            ConstraintCount = 17,
            Mappings = 18,
        });

        Assert.Equal(PhysicsLoadStatus.Ok, report.Status);
        Assert.DoesNotContain("older hkp format", report.Headline);
    }

    [Fact]
    public void ARagdollWithNoUsableBodiesIsNoBodies()
    {
        var report = PhysicsLoadClassifier.Classify(Ragdoll() with
        {
            BodyCount = 18,
            UsableBodies = 0,
            SkippedBodies = 18,
        });

        Assert.Equal(PhysicsLoadStatus.NoBodies, report.Status);
        Assert.Equal(PhysicsLoadSeverity.Error, report.Severity);
        Assert.Contains("18 of its 18 bodies could not be read", report.Headline);
    }

    [Fact]
    public void ARagdollThatDeclaresNoBodiesAtAllIsNoBodies()
    {
        var report = PhysicsLoadClassifier.Classify(Ragdoll() with { BodyCount = 0, UsableBodies = 0 });

        Assert.Equal(PhysicsLoadStatus.NoBodies, report.Status);
        Assert.Contains("declares no rigid bodies at all", report.Headline);
    }

    [Fact]
    public void ARagdollWithNoUsableMapperIsMapperMissing()
    {
        var report = PhysicsLoadClassifier.Classify(Ragdoll() with { Mappings = 0 });

        Assert.Equal(PhysicsLoadStatus.MapperMissing, report.Status);
        Assert.Equal(PhysicsLoadSeverity.Warning, report.Severity);
        Assert.Contains("hkaSkeletonMapper", report.Headline);
        Assert.Contains("is present but none of its mappings link this ragdoll to the animation skeleton",
                        report.Headline);
        Assert.Contains("cannot follow the animation", report.Headline);
    }

    [Fact]
    public void ARagdollWithNoMapperClassSaysTheMapperIsAbsent()
    {
        var report = PhysicsLoadClassifier.Classify(Ragdoll() with
        {
            ClassCounts = new Dictionary<string, int>
            {
                ["hknpRagdollData"] = 1,
                ["hknpCapsuleShape"] = 18,
                ["hkaSkeleton"] = 1,
            },
            Mappings = 0,
        });

        Assert.Equal(PhysicsLoadStatus.MapperMissing, report.Status);
        Assert.False(report.MapperPresent);
        Assert.Contains("no hkaSkeletonMapper links the animation skeleton to it", report.Headline);
        Assert.Contains("cannot follow the animation", report.Headline);
    }

    [Fact]
    public void UnsupportedShapeAndSkippedEntryCountsSurviveAnotherPrimaryStatus()
    {
        var report = PhysicsLoadClassifier.Classify(Ragdoll() with
        {
            BodyCount = 18,
            UsableBodies = 18,
            UnsupportedShapeBodies = 3,
            UnsupportedShapeClasses = new Dictionary<string, int> { ["hknpConvexPolytopeShape"] = 3 },
            SkippedConstraints = 2,
            Mappings = 0,
        });

        Assert.Equal(PhysicsLoadStatus.MapperMissing, report.Status);
        Assert.Contains("is present but none of its mappings link this ragdoll", report.Headline);
        Assert.Contains(report.Findings, finding =>
            finding.Severity == PhysicsLoadSeverity.Info &&
            finding.Message.Contains("3 of 18 bodies use shapes BGS cannot preview yet") &&
            finding.Message.Contains("hknpConvexPolytopeShape x3"));
        Assert.Contains(report.Findings, finding =>
            finding.Severity == PhysicsLoadSeverity.Info &&
            finding.Message.Contains("2 constraints entries could not be read"));
    }

    [Fact]
    public void AMixedLegacyAndRagdollFileStaysOkWithAnInfoLegacyFinding()
    {
        var counts = new Dictionary<string, int>
        {
            ["hknpRagdollData"] = 1,
            ["hknpCapsuleShape"] = 18,
            ["hkaSkeletonMapper"] = 1,
            ["hkpRigidBody"] = 2,
            ["hkpSphereShape"] = 2,
        };
        var report = PhysicsLoadClassifier.Classify(Ragdoll() with { ClassCounts = counts });

        Assert.Equal(PhysicsLoadStatus.Ok, report.Status);
        Assert.Equal(PhysicsLoadSeverity.Info, report.Severity);
        Assert.Contains(report.Findings, finding =>
            finding.Severity == PhysicsLoadSeverity.Info &&
            finding.Message.Contains("older hkp objects") &&
            finding.Message.Contains("hkpRigidBody x2"));
    }

    [Fact]
    public void ALegacyObjectBesideAnUnusableRagdollIsStillNamed()
    {
        var counts = new Dictionary<string, int>
        {
            ["hknpRagdollData"] = 1,
            ["hkpRigidBody"] = 1,
        };
        var report = PhysicsLoadClassifier.Classify(Ragdoll() with
        {
            ClassCounts = counts,
            BodyCount = 0,
            UsableBodies = 0,
        });

        Assert.Equal(PhysicsLoadStatus.NoBodies, report.Status);
        Assert.Contains(report.Findings, finding =>
            finding.Severity == PhysicsLoadSeverity.Info &&
            finding.Message.Contains("older hkp objects") &&
            finding.Message.Contains("hkpRigidBody x1"));
    }

    [Fact]
    public void BodiesWithShapesBgsCannotPreviewAreUnsupportedShapes()
    {
        var report = PhysicsLoadClassifier.Classify(Ragdoll() with
        {
            BodyCount = 18,
            UsableBodies = 18,
            UnsupportedShapeBodies = 3,
            UnsupportedShapeClasses = new Dictionary<string, int> { ["hknpSphereShape"] = 2, ["hknpConvexPolytopeShape"] = 1 },
            ShapeCount = 15,
        });

        Assert.Equal(PhysicsLoadStatus.UnsupportedShapes, report.Status);
        Assert.Equal(PhysicsLoadSeverity.Warning, report.Severity);
        Assert.Contains("3 of 18 bodies use shapes BGS cannot preview yet", report.Headline);
        Assert.Contains("hknpSphereShape x2", report.Headline);
        Assert.Contains("hknpConvexPolytopeShape x1", report.Headline);
        Assert.Equal(3, report.UnsupportedShapeBodies);
    }

    [Fact]
    public void ABodyEntryThatCouldNotBeReadIsCountedNotSilent()
    {
        var report = PhysicsLoadClassifier.Classify(Ragdoll() with
        {
            BodyCount = 18,
            UsableBodies = 17,
            SkippedBodies = 1,
        });

        Assert.Equal(PhysicsLoadStatus.UnsupportedShapes, report.Status);
        Assert.Contains("1 body entry could not be read", report.Headline);
    }

    [Fact]
    public void AConstraintEntryThatCouldNotBeReadIsCountedNotSilent()
    {
        var report = PhysicsLoadClassifier.Classify(Ragdoll() with { SkippedConstraints = 2 });

        Assert.Equal(PhysicsLoadStatus.UnsupportedShapes, report.Status);
        Assert.Contains("2 constraints entries could not be read", report.Headline);
        Assert.Contains("constraints: 12 read, 2 skipped", report.Diagnostics("x.hkx"));
    }

    [Fact]
    public void AWrongContainerNamesWhatWasDetected()
    {
        var report = PhysicsLoadClassifier.Classify(new PhysicsLoadFacts
        {
            ContainerProblem = "it uses 4-byte pointers, and Fallout 4 uses 8-byte pointers",
        });

        Assert.Equal(PhysicsLoadStatus.WrongContainer, report.Status);
        Assert.Equal(PhysicsLoadSeverity.Error, report.Severity);
        Assert.Contains("not a Fallout 4 physics packfile", report.Headline);
        Assert.Contains("4-byte pointers", report.Headline);
    }

    [Fact]
    public void AParseErrorNamesTheStage()
    {
        var report = PhysicsLoadClassifier.Classify(new PhysicsLoadFacts
        {
            ParseStage = "byte-backed XML",
            ParseMessage = "the section table is not monotonic",
        });

        Assert.Equal(PhysicsLoadStatus.ParseError, report.Status);
        Assert.Equal("byte-backed XML", report.FailingStage);
        Assert.Contains("byte-backed XML", report.Headline);
        Assert.Contains("the section table is not monotonic", report.Headline);
    }

    [Fact]
    public void EveryStatusReadsInPlainWords()
    {
        var reports = new List<PhysicsLoadReport>
        {
            PhysicsLoadClassifier.Classify(Ragdoll()),
            PhysicsLoadClassifier.Classify(new PhysicsLoadFacts
            {
                ClassCounts = new Dictionary<string, int> { ["hkaSkeleton"] = 1 },
            }),
            PhysicsLoadClassifier.Classify(new PhysicsLoadFacts
            {
                ClassCounts = new Dictionary<string, int> { ["hkpRigidBody"] = 1 },
            }),
            PhysicsLoadClassifier.Classify(Ragdoll() with { UnsupportedShapeBodies = 1, BodyCount = 18 }),
            PhysicsLoadClassifier.Classify(Ragdoll() with { Mappings = 0 }),
            PhysicsLoadClassifier.Classify(Ragdoll() with { UsableBodies = 0, SkippedBodies = 1, BodyCount = 1 }),
            PhysicsLoadClassifier.Classify(new PhysicsLoadFacts { ContainerProblem = "the file is empty" }),
            PhysicsLoadClassifier.Classify(new PhysicsLoadFacts { ParseStage = "container", ParseMessage = "x" }),
        };

        Assert.Equal(8, reports.Count);
        foreach (var report in reports)
        {
            Assert.False(string.IsNullOrWhiteSpace(report.Headline));
            Assert.True(report.Headline.Length > 40, $"'{report.Status}' explains itself in one sentence");
            Assert.DoesNotContain(report.StatusSlug, report.Headline);
            Assert.DoesNotContain(report.StatusSlug, report.Summary);
            foreach (var finding in report.Findings)
                Assert.False(string.IsNullOrWhiteSpace(finding.Message));
        }

        Assert.Equal(Enum.GetValues<PhysicsLoadStatus>().Length,
                     reports.Select(report => report.Status).Distinct().Count());
    }

    [Fact]
    public void NullAndEmptyInputStayNullAndSayWhy()
    {
        Assert.Null(HavokPhysicsExtractor.TryExtract(null, out var missing));
        Assert.Equal(PhysicsLoadStatus.WrongContainer, missing.Status);
        Assert.Contains("empty", missing.Headline);

        Assert.Null(HavokPhysicsExtractor.TryExtract(Array.Empty<byte>(), out var blank));
        Assert.Equal(PhysicsLoadStatus.WrongContainer, blank.Status);

        Assert.Null(HavokPhysicsExtractor.TryExtract(new byte[] { 0, 1, 2, 3, 4, 5, 6, 7 }, out var tiny));
        Assert.Equal(PhysicsLoadStatus.WrongContainer, tiny.Status);
        Assert.Contains("too small", tiny.Headline);
    }

    [Fact]
    public void FourBytePointersAreRefusedByTheExtractorWithTheReason()
    {
        byte[] bytes = File.ReadAllBytes(Fixture(Skeleton));
        bytes[0x10] = 4;

        var model = HavokPhysicsExtractor.TryExtract(bytes, out var report);

        Assert.Null(model);
        Assert.Equal(PhysicsLoadStatus.WrongContainer, report.Status);
        Assert.Contains("4-byte pointers", report.Headline);
        Assert.Equal(4, report.PointerSize);
        Assert.Equal(1, report.CountOf("hknpRagdollData"));
        Assert.Equal(0, report.UsableBodies);
    }

    [Fact]
    public void AnUnusualContainerThatStillReadsIsAnInfoFindingNotAStatus()
    {
        var report = PhysicsLoadClassifier.Classify(Ragdoll() with
        {
            FileVersion = 10,
            ContainerNotice = "it is a version 10 packfile, and Fallout 4 uses version 11",
        });

        Assert.Equal(PhysicsLoadStatus.Ok, report.Status);
        Assert.Equal(PhysicsLoadSeverity.Info, report.Severity);
        Assert.Contains(report.Findings, finding =>
            finding.Severity == PhysicsLoadSeverity.Info &&
            finding.Message.Contains("version 10 packfile"));
    }

    [Fact]
    public void AVersionTenContainerNoticeComesFromTheRealExtractor()
    {
        byte[] bytes = File.ReadAllBytes(Fixture(Skeleton));
        bytes[0x0C] = 10;

        var model = HavokPhysicsExtractor.TryExtract(bytes, out var report);

        Assert.NotNull(model);
        Assert.Equal(18, model!.Bodies.Count);
        Assert.Equal(PhysicsLoadStatus.Ok, report.Status);
        Assert.Equal(10, report.FileVersion);
        Assert.Contains(report.Findings, finding =>
            finding.Severity == PhysicsLoadSeverity.Info &&
            finding.Message.Contains("version 10 packfile"));
    }

    [Fact]
    public void ASectionCountPastTheEndOfTheFileIsAParseError()
    {
        byte[] bytes = File.ReadAllBytes(Fixture(Skeleton));
        bytes[0x14] = 0xFF;
        bytes[0x15] = 0xFF;
        bytes[0x16] = 0xFF;
        bytes[0x17] = 0x7F;

        var model = HavokPhysicsExtractor.TryExtract(bytes, out var report);

        Assert.Null(model);
        Assert.Equal(PhysicsLoadStatus.ParseError, report.Status);
        Assert.Equal("container", report.FailingStage);
        Assert.Contains("failed at the container stage", report.Headline);
        Assert.Equal(11, report.FileVersion);
    }

    [Fact]
    public void ABinaryTagfileIsRefusedByItsTag0Marker()
    {
        var bytes = new byte[PackfileImage.HeaderSize + 8];
        bytes[0] = 0x57; bytes[1] = 0xE0; bytes[2] = 0xE0; bytes[3] = 0x57;
        bytes[4] = (byte)'T'; bytes[5] = (byte)'A'; bytes[6] = (byte)'G'; bytes[7] = (byte)'0';

        var model = HavokPhysicsExtractor.TryExtract(bytes, out var report);

        Assert.Null(model);
        Assert.Equal(PhysicsLoadStatus.WrongContainer, report.Status);
        Assert.Contains("tagfile", report.Headline);
    }

    [Fact]
    public void ARagdollWithNoSkeletonClassNamesTheAbsentMapperThroughTheExtractor()
    {
        byte[] bytes = ReplaceClassName(File.ReadAllBytes(Fixture(Skeleton)), "hkaSkeleton", "hkxSkeleton");

        var model = HavokPhysicsExtractor.TryExtract(bytes, out var report);

        Assert.NotNull(model);
        Assert.Equal(18, model!.Bodies.Count);
        Assert.Equal(PhysicsLoadStatus.MapperMissing, report.Status);
        Assert.False(report.MapperPresent);
        Assert.Equal(0, report.CountOf("hkaSkeleton"));
        Assert.Contains("no hkaSkeletonMapper links the animation skeleton to it", report.Headline);
    }

    [Fact]
    public void AnXmlTagfileIsRefusedByTheExtractorWithTheReason()
    {
        string xml = "<?xml version=\"1.0\" encoding=\"ascii\"?>\n<hkpackfile classversion=\"11\" contentsversion=\"hk_2014.1.0-r1\">\n</hkpackfile>\n";
        Assert.True(Encoding.ASCII.GetByteCount(xml) > PackfileImage.HeaderSize);

        var model = HavokPhysicsExtractor.TryExtract(Encoding.ASCII.GetBytes(xml), out var report);

        Assert.Null(model);
        Assert.Equal(PhysicsLoadStatus.WrongContainer, report.Status);
        Assert.Contains("tagfile", report.Headline);
    }

    [Fact]
    public void TheClothHairFixtureIsSkeletonOnlyThroughTheExtractor()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(Fixture(ClothHair)), out var report);

        Assert.Null(model);
        Assert.Equal(PhysicsLoadStatus.SkeletonOnly, report.Status);
        Assert.Equal(11, report.FileVersion);
        Assert.Equal(1, report.CountOf("hkaSkeleton"));
    }

    [Fact]
    public void ALegacyFileIsReportedAsLegacyPhysicsThroughTheExtractor()
    {
        byte[] bytes = ReplaceClassName(File.ReadAllBytes(Fixture(Skeleton)), "hknpRagdollData", "hkpCapsuleShape");

        var model = HavokPhysicsExtractor.TryExtract(bytes, out var report);

        Assert.Null(model);
        Assert.Equal(PhysicsLoadStatus.LegacyPhysics, report.Status);
        Assert.Equal(0, report.CountOf("hknpRagdollData"));
        Assert.Equal(1, report.CountOf("hkpCapsuleShape"));
    }

    [Fact]
    public void TheDiagnosticsDumpCarriesEverythingSupportNeeds()
    {
        HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(Fixture(Skeleton)), out var report);

        string text = report.Diagnostics(@"C:\Mods\skeleton.hkx", 43296, "Behaviour Graph Studio 1.3.0 · build local");

        Assert.Contains("Behaviour Graph Studio 1.3.0", text);
        Assert.Contains(@"file: C:\Mods\skeleton.hkx", text);
        Assert.Contains("size: 43296 bytes", text);
        Assert.Contains("file version: 11", text);
        Assert.Contains("pointer size: 8", text);
        Assert.Contains("status: OK (ok)", text);
        Assert.Contains("hknpCapsuleShape x19", text);
        Assert.Contains("mapper: present (18 mappings)", text);
        Assert.Contains("hknpBSMaterial: absent", text);
        Assert.Contains("shapes: 19 read", text);
        Assert.Contains("constraints: 17 read, 0 skipped", text);
        Assert.Contains("shape classes: hknpCapsuleShape x19", text);
        Assert.Contains("constraint classes: hkpRagdollConstraintData x9, hkpLimitedHingeConstraintData x8", text);
        Assert.Contains("class counts:", text);
    }

    [Fact]
    public void TheDiagnosticsDumpExplainsAFailure()
    {
        byte[] bytes = File.ReadAllBytes(Fixture(Skeleton));
        bytes[0x10] = 4;
        HavokPhysicsExtractor.TryExtract(bytes, out var report);

        string text = report.Diagnostics("skeleton.hkx");

        Assert.Contains("status: WRONG CONTAINER (wrong-container)", text);
        Assert.Contains("pointer size: 4", text);
        Assert.Contains("size: unknown", text);
        Assert.Contains("[error] This file is not a Fallout 4 physics packfile", text);
        Assert.Contains("next: ", text);
    }

    private static PhysicsLoadFacts Ragdoll() => new()
    {
        FileVersion = 11,
        PointerSize = 8,
        ClassCounts = new Dictionary<string, int>
        {
            ["hknpRagdollData"] = 1,
            ["hknpCapsuleShape"] = 18,
            ["hknpPhysicsSceneData"] = 1,
            ["hkaSkeleton"] = 2,
            ["hkaSkeletonMapper"] = 2,
        },
        BodyCount = 18,
        UsableBodies = 18,
        ShapeCount = 18,
        ConstraintCount = 12,
        Mappings = 18,
    };

    private static byte[] ReplaceClassName(byte[] bytes, string from, string to)
    {
        Assert.Equal(from.Length, to.Length);
        byte[] needle = Encoding.ASCII.GetBytes(from);
        byte[] replacement = Encoding.ASCII.GetBytes(to);

        int hits = 0;
        for (int at = 0; at + needle.Length <= bytes.Length; at++)
        {
            bool same = true;
            for (int i = 0; i < needle.Length && same; i++) same = bytes[at + i] == needle[i];
            if (!same) continue;

            replacement.CopyTo(bytes, at);
            hits++;
        }

        Assert.True(hits > 0, $"{from} does not appear in the file");
        return bytes;
    }
}
