using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using BehaviourStudio.App;
using OpenCommonwealth.Services.Archive;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class RagdollSiblingTests
{
    private static PhysicsLoadReport SkeletonOnly() => PhysicsLoadClassifier.Classify(new PhysicsLoadFacts
    {
        ClassCounts = new Dictionary<string, int> { ["hkaSkeleton"] = 1 },
    });

    private static PhysicsLoadReport Ragdoll() => PhysicsLoadClassifier.Classify(new PhysicsLoadFacts
    {
        FileVersion = 11,
        PointerSize = 8,
        ClassCounts = new Dictionary<string, int>
        {
            ["hknpRagdollData"] = 1,
            ["hknpCapsuleShape"] = 18,
            ["hkaSkeleton"] = 1,
            ["hkaSkeletonMapper"] = 1,
        },
        BodyCount = 18,
        UsableBodies = 18,
        ShapeCount = 18,
        ConstraintCount = 17,
        Mappings = 18,
    });

    private static HavokRagdollModel OneBody()
    {
        var model = new HavokRagdollModel();
        model.Bodies.Add(new HavokRigidBody { Id = 0, Name = "root" });
        return model;
    }

    [Fact]
    public void ASkeletonOnlySiblingLeavesTheFilesOwnReport()
    {
        var own = SkeletonOnly();

        var choice = MainWindow.ChooseRagdoll("clip.hkx", null, own, "skeleton.hkx",
            _ => (null, SkeletonOnly()));

        Assert.Same(own, choice.Report);
        Assert.Equal("clip.hkx", choice.ReportPath);
        Assert.Null(choice.SourcePath);
        Assert.Null(choice.Model);
    }

    [Fact]
    public void EverySiblingReportExceptSkeletonOnlyIsPreferred()
    {
        var siblingReports = new[]
        {
            PhysicsLoadClassifier.Classify(new PhysicsLoadFacts
            {
                ParseStage = "container",
                ParseMessage = "the section table is not monotonic",
            }),
            PhysicsLoadClassifier.Classify(new PhysicsLoadFacts
            {
                ContainerProblem = "it uses 4-byte pointers, and Fallout 4 uses 8-byte pointers",
            }),
            PhysicsLoadClassifier.Classify(new PhysicsLoadFacts
            {
                FileVersion = 11,
                PointerSize = 8,
                ClassCounts = new Dictionary<string, int> { ["hknpRagdollData"] = 1 },
                BodyCount = 18,
                UsableBodies = 0,
                SkippedBodies = 18,
            }),
            PhysicsLoadClassifier.Classify(new PhysicsLoadFacts
            {
                FileVersion = 11,
                PointerSize = 8,
                ClassCounts = new Dictionary<string, int>
                {
                    ["hknpRagdollData"] = 1,
                    ["hknpCapsuleShape"] = 18,
                    ["hkaSkeleton"] = 1,
                },
                BodyCount = 18,
                UsableBodies = 18,
                ShapeCount = 18,
                ConstraintCount = 17,
            }),
        };

        Assert.Equal(4, siblingReports.Select(report => report.Status).Distinct().Count());
        foreach (var siblingReport in siblingReports)
        {
            var choice = MainWindow.ChooseRagdoll("clip.hkx", null, SkeletonOnly(), "skeleton.hkx",
                _ => (null, siblingReport));

            Assert.NotEqual(PhysicsLoadStatus.SkeletonOnly, siblingReport.Status);
            Assert.Same(siblingReport, choice.Report);
            Assert.Equal("skeleton.hkx", choice.ReportPath);
            Assert.Null(choice.SourcePath);
        }
    }

    [Fact]
    public void ASiblingReadFailureBecomesAParseErrorAtTheSiblingReadStage()
    {
        var choice = MainWindow.ChooseRagdoll("clip.hkx", null, SkeletonOnly(), "skeleton.hkx",
            _ => throw new IOException("the file is in use"));

        Assert.Null(choice.Model);
        Assert.NotNull(choice.Report);
        Assert.Equal(PhysicsLoadStatus.ParseError, choice.Report!.Status);
        Assert.Equal("sibling read", choice.Report.FailingStage);
        Assert.Contains("the file is in use", choice.Report.Headline);
        Assert.Equal("skeleton.hkx", choice.ReportPath);
        Assert.Null(choice.SourcePath);
    }

    [Fact]
    public void ASiblingRagdollIsAdoptedWithItsPathAsTheSource()
    {
        var siblingModel = OneBody();
        var siblingReport = Ragdoll();

        var choice = MainWindow.ChooseRagdoll("clip.hkx", null, SkeletonOnly(), "skeleton.hkx",
            _ => (siblingModel, siblingReport));

        Assert.Same(siblingModel, choice.Model);
        Assert.Same(siblingReport, choice.Report);
        Assert.Equal("skeleton.hkx", choice.ReportPath);
        Assert.Equal("skeleton.hkx", choice.SourcePath);
    }

    [Fact]
    public void TheFilesOwnRagdollWinsOverTheSibling()
    {
        var ownModel = OneBody();
        var ownReport = Ragdoll();
        bool asked = false;

        var choice = MainWindow.ChooseRagdoll("clip.hkx", ownModel, ownReport, "skeleton.hkx", _ =>
        {
            asked = true;
            return (null, SkeletonOnly());
        });

        Assert.False(asked);
        Assert.Same(ownModel, choice.Model);
        Assert.Same(ownReport, choice.Report);
        Assert.Equal("clip.hkx", choice.ReportPath);
        Assert.Null(choice.SourcePath);
    }

    [Fact]
    public void AMissingSiblingLeavesTheFilesOwnReport()
    {
        var own = SkeletonOnly();

        var choice = MainWindow.ChooseRagdoll("clip.hkx", null, own, null, _ => throw new IOException("never called"));

        Assert.Same(own, choice.Report);
        Assert.Equal("clip.hkx", choice.ReportPath);
        Assert.Null(choice.SourcePath);
    }

    [Fact]
    public void ThePackedSkeletonComesFromTheSkeletonEntryRatherThanTheAnimation()
    {
        using var root = new TestArchives.TempDirectory("bgs-ragdoll-packed");
        string dataFolder = Path.Combine(root.Path, "Data");
        Directory.CreateDirectory(dataFolder);
        byte[] skeleton = { 5, 5, 5, 5 };
        TestArchives.WriteGeneralBa2(Path.Combine(dataFolder, "Fallout4 - Animations.ba2"),
            ("Meshes/Actors/Turret/Animations/Idle.hkx", new byte[] { 1, 1 }),
            ("Meshes/Actors/Turret/CharacterAssets/skeleton.hkx", skeleton));

        using var data = GameData.Discover(dataFolder, Path.Combine(root.Path, "plugins.txt"));
        string animation = data.DataRelativePath(Path.Combine(dataFolder, "Meshes", "Actors", "Turret",
                                                              "Animations", "Idle.hkx"))!;
        Assert.Equal("Meshes/Actors/Turret/Animations/Idle.hkx", animation);

        string? sibling = data.SkeletonEntryName(animation);
        Assert.Equal("Meshes/Actors/Turret/CharacterAssets/skeleton.hkx", sibling);
        Assert.Equal(skeleton, MainWindow.PackedSkeleton(data, animation, sibling!));
        Assert.Equal(skeleton, data.SkeletonBytes(animation));

        var choice = MainWindow.ChooseRagdoll(animation, null, SkeletonOnly(), sibling,
            _ => (OneBody(), Ragdoll()));

        Assert.Equal("Meshes/Actors/Turret/CharacterAssets/skeleton.hkx", choice.SourcePath);
        Assert.Equal(sibling, choice.ReportPath);
        Assert.NotNull(choice.Model);
    }

    [Fact]
    public void ATransientCopyOfAPackedClipResolvesItsPackedSkeletonFromTheVirtualPathHint()
    {
        using var root = new TestArchives.TempDirectory("bgs-ragdoll-hint");
        string dataFolder = Path.Combine(root.Path, "Data");
        Directory.CreateDirectory(dataFolder);
        byte[] skeleton = { 7, 7, 7, 7 };
        TestArchives.WriteGeneralBa2(Path.Combine(dataFolder, "Fallout4 - Animations.ba2"),
            ("Meshes/Actors/Turret/Animations/Idle.hkx", new byte[] { 1, 1 }),
            ("Meshes/Actors/Turret/CharacterAssets/skeleton.hkx", skeleton));

        using var data = GameData.Discover(dataFolder, Path.Combine(root.Path, "plugins.txt"));
        using var temp = new TestArchives.TempDirectory("bgs-ragdoll-copy");
        string copy = Path.Combine(temp.Path, "Idle.hkx");
        File.WriteAllBytes(copy, new byte[] { 1, 1 });

        Assert.Null(data.DataRelativePath(copy));
        Assert.Null(MainWindow.PackedAnimationKey(data, copy, null));

        const string hint = "Meshes/Actors/Turret/Animations/Idle.hkx";
        Assert.Equal(hint, MainWindow.PackedAnimationKey(data, copy, hint));
        Assert.Null(MainWindow.PackedAnimationKey(data, copy, "../escape.hkx"));

        string? sibling = data.SkeletonEntryName(hint);
        Assert.Equal("Meshes/Actors/Turret/CharacterAssets/skeleton.hkx", sibling);
        Assert.Equal(skeleton, MainWindow.PackedSkeleton(data, hint, sibling!));

        var choice = MainWindow.ChooseRagdoll(copy, null, SkeletonOnly(), sibling,
            _ => (OneBody(), Ragdoll()));

        Assert.Equal(sibling, choice.ReportPath);
        Assert.NotNull(choice.Model);
    }
}
