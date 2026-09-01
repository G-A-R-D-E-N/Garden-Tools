using System;
using System.IO;
using System.Linq;
using System.Numerics;
using BehaviourStudio.App;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class RagdollFrameRegressionTests
{
    private const string Skeleton =
        "Meshes/Actors/Character/CharacterAssets/skeleton.hkx";

    private static string Fixture(string relative) =>
        Path.Combine(AppContext.BaseDirectory, "fixtures/vanilla",
                     relative.Replace('/', Path.DirectorySeparatorChar));

    [Fact]
    public void StaticDropStartsFromTheMeasuredBodyFramesAlreadyOnScreen()
    {
        string path = Fixture(Skeleton);
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(path));
        Assert.NotNull(model);

        var view = new SkeletonView();
        view.SetBodies(model);
        view.StartDrop();

        Assert.True(view.IsDropped);
        Assert.NotNull(view.DroppedPositions);
        Assert.NotNull(view.DroppedRotations);

        foreach (var binding in model!.BoneBindings)
        {
            var body = model.Bodies.FirstOrDefault(candidate => candidate.Id == binding.BodyId);
            if (body == null || binding.BoneIndex < 0 || binding.BoneIndex >= view.DroppedPositions!.Count)
                continue;

            Assert.True(Vector3.Distance(body.Position, view.DroppedPositions[binding.BoneIndex]) <= 5e-3f,
                $"body {body.Id} changed position when the static drop was captured");
            Assert.True(MathF.Abs(Quaternion.Dot(Quaternion.Normalize(body.Rotation),
                                                 Quaternion.Normalize(view.DroppedRotations![binding.BoneIndex]))) >= 0.999f,
                $"body {body.Id} changed rotation when the static drop was captured");
        }
    }
}
