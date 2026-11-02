using System;
using System.IO;
using System.Linq;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class CanonicalPackfileWriterTests
{
    private static string FixturesRoot => Path.Combine(AppContext.BaseDirectory, "fixtures", "vanilla");

    private static string SkeletonFixture => Path.Combine(FixturesRoot, "Meshes", "Actors", "Character",
                                                          "CharacterAssets", "skeleton.hkx");

    [Fact]
    public void AnUneditedVanillaSkeletonReserializesToItsOwnBytes()
    {
        byte[] original = File.ReadAllBytes(SkeletonFixture);
        var image = PackfileImage.Read(original);

        byte[]? output = CanonicalPackfileWriter.Write(image);

        Assert.NotNull(output);
        Assert.Equal(original, output);
    }

    [Fact]
    public void AnInsertedBoneRowRoundTripsBackToTheOriginalBytes()
    {
        byte[] original = File.ReadAllBytes(SkeletonFixture);

        var first = PackfileImage.Read(original);
        var (payload, count) = Bones(first);
        Assert.True(count > 1, "the fixture holds too few bones to grow");

        byte[]? inserted = CanonicalPackfileWriter.Write(
            first, new[] { new CanonicalPackfileWriter.Resize(payload, count + 1) });
        Assert.NotNull(inserted);
        Assert.True(inserted!.Length > original.Length);

        var reread = PackfileImage.Read(inserted);
        var (_, movedCount) = Bones(reread);
        Assert.Equal(count + 1, movedCount);

        byte[]? again = CanonicalPackfileWriter.Write(reread);
        Assert.NotNull(again);
        Assert.Equal(inserted, again);

        var second = PackfileImage.Read(inserted);
        var (secondPayload, secondCount) = Bones(second);
        byte[]? removed = CanonicalPackfileWriter.Write(
            second, new[] { new CanonicalPackfileWriter.Resize(secondPayload, secondCount - 1) });

        Assert.NotNull(removed);
        Assert.Equal(original, removed);
    }

    [Fact]
    public void ResizingSomethingThatIsNotAnArrayIsRefusedRatherThanGuessedAt()
    {
        byte[] original = File.ReadAllBytes(SkeletonFixture);

        var first = PackfileImage.Read(original);
        Assert.Null(CanonicalPackfileWriter.Write(first, new[] { new CanonicalPackfileWriter.Resize(4, 8) }));

        var second = PackfileImage.Read(original);
        Assert.Null(CanonicalPackfileWriter.Write(second, new[] { new CanonicalPackfileWriter.Resize(0, -1) }));
    }

    [Fact]
    public void AFileWithAClassThisBuildDoesNotKnowIsRefused()
    {
        byte[] original = File.ReadAllBytes(SkeletonFixture);
        var image = PackfileImage.Read(original);
        var names = image.Section("__classnames__")!;
        var data = image.Section("__data__")!;

        int nameAt = NativeAppend.NameOffset(names, "hclClothSetupObject", 0x12345678);
        var virtuals = data.Virtuals().ToList();
        virtuals[0] = (virtuals[0].Source, image.Sections.IndexOf(names), nameAt);
        data.SetVirtuals(virtuals);

        Assert.Null(CanonicalPackfileWriter.Write(image));
    }

    private static (int Payload, int Count) Bones(PackfileImage image)
    {
        var objects = new PackfileObjects(image);
        foreach (var owner in objects.OfClass("hkaSkeleton"))
        {
            if (objects.FieldAt(owner, "bones") is not int header) continue;
            var array = objects.ArrayAt(header);
            if (array != null && array.Count > 0) return (array.At, array.Count);
        }

        throw new InvalidOperationException("the fixture holds no hkaSkeleton bone array");
    }
}
