using System;
using System.IO;
using System.Linq;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class SkeletonMapperReaderProofTests
{
    private static string Fixture(string relative) =>
        Path.Combine(AppContext.BaseDirectory, "fixtures/vanilla",
                     relative.Replace('/', Path.DirectorySeparatorChar));

    private const string Skeleton =
        "Meshes/Actors/Character/CharacterAssets/skeleton.hkx";

    [Fact]
    public void VanillaSkeletonMapperNeedsAReusableCoreReader()
    {
        var image = PackfileImage.Read(File.ReadAllBytes(Fixture(Skeleton)));
        var objects = new PackfileObjects(image);

        Assert.NotEmpty(objects.Instances.Where(instance => instance.ClassName == "hkaSkeletonMapper"));

        var reader = typeof(PackfileImage).Assembly.GetType(
            "OpenCommonwealth.Services.Hkx.SkeletonMapperReader");
        Assert.NotNull(reader);
    }
}
