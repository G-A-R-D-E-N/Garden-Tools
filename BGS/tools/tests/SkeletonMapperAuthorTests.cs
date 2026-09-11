using System.Numerics;
using OpenCommonwealth.Services.Hkx;
using Xunit;
using Xunit.Abstractions;

namespace BehaviourStudio.Tests;

public class SkeletonMapperAuthorTests
{
    private readonly ITestOutputHelper _out;

    public SkeletonMapperAuthorTests(ITestOutputHelper output) => _out = output;

    [Fact]
    public void AuthoredAnimationToRagdollRowsMatchTheShippedMapper()
    {
        var (forward, _) = Vanilla();
        Assert.Equal(SkeletonMapperAuthor.RagdollMappingType, forward.MappingType);

        var pairs = forward.SimpleMappings.Select(row => (row.BoneA, row.BoneB)).ToList();
        var authored = SkeletonMapperAuthor.FromReferencePose(forward.SkeletonA, forward.SkeletonB, pairs);

        Assert.Equal(forward.SimpleMappings.Count, authored.Count);
        Assert.True(authored.Count > 0);
        Compare(forward.SimpleMappings, authored);
    }

    [Fact]
    public void AuthoredRagdollToAnimationRowsMatchTheShippedMapper()
    {
        var (_, reverse) = Vanilla();

        var pairs = reverse.SimpleMappings.Select(row => (row.BoneA, row.BoneB)).ToList();
        var authored = SkeletonMapperAuthor.FromReferencePose(reverse.SkeletonA, reverse.SkeletonB, pairs);

        Assert.Equal(reverse.SimpleMappings.Count, authored.Count);
        Compare(reverse.SimpleMappings, authored);
    }

    [Fact]
    public void ReversingTheShippedRowsReproducesTheShippedReciprocalMapper()
    {
        var (forward, reverse) = Vanilla();

        var reversed = SkeletonMapperAuthor.Reverse(forward.SimpleMappings);
        var mirrored = reversed.ToDictionary(row => (row.BoneA, row.BoneB));

        Assert.Equal(reverse.SimpleMappings.Count, reversed.Count);
        foreach (var shipped in reverse.SimpleMappings)
        {
            Assert.True(mirrored.TryGetValue((shipped.BoneA, shipped.BoneB), out var authored),
                        $"no reversed row for {shipped.BoneA} -> {shipped.BoneB}");
            AssertClose(shipped.AFromBTranslation, authored.AFromBTranslation, 5e-4f,
                        $"translation {shipped.BoneA} -> {shipped.BoneB}");
            AssertRotationClose(shipped.AFromBRotation, authored.AFromBRotation, 1e-5f,
                                $"rotation {shipped.BoneA} -> {shipped.BoneB}");
        }
    }

    [Fact]
    public void ReciprocalPairCoversBothDirectionsOfTheSameBonePairs()
    {
        var (forward, _) = Vanilla();
        var pairs = forward.SimpleMappings.Select(row => (row.BoneA, row.BoneB)).ToList();

        var pair = SkeletonMapperAuthor.Reciprocal(forward.SkeletonA, forward.SkeletonB, pairs);

        Assert.Equal(pairs.Count, pair.Forward.Count);
        Assert.Equal(pairs.Count, pair.Reverse.Count);
        for (int i = 0; i < pairs.Count; i++)
        {
            Assert.Equal(pairs[i].BoneA, pair.Reverse[i].BoneB);
            Assert.Equal(pairs[i].BoneB, pair.Reverse[i].BoneA);
        }
    }

    [Fact]
    public void MappingOneBoneTwiceIsRefused()
    {
        var (forward, _) = Vanilla();

        string firstA = forward.SkeletonA.BoneNames[0];
        var refusals = SkeletonMapperAuthor.Refusals(forward.SkeletonA, forward.SkeletonB,
                                                     new[] { (0, 0), (0, 1) });
        Assert.Contains(refusals, refusal => refusal.Contains("already mapped", StringComparison.Ordinal) &&
                                             refusal.Contains(firstA, StringComparison.Ordinal));

        string firstB = forward.SkeletonB.BoneNames[0];
        refusals = SkeletonMapperAuthor.Refusals(forward.SkeletonA, forward.SkeletonB,
                                                 new[] { (0, 0), (1, 0) });
        Assert.Contains(refusals, refusal => refusal.Contains("already mapped", StringComparison.Ordinal) &&
                                             refusal.Contains(firstB, StringComparison.Ordinal));
    }

    [Fact]
    public void SelfPairingAcrossReciprocalSkeletonsIsNotARefusal()
    {
        var (forward, _) = Vanilla();

        var refusals = SkeletonMapperAuthor.Refusals(forward.SkeletonA, forward.SkeletonB,
                                                     forward.SimpleMappings.Select(row => (row.BoneA, row.BoneB)).ToList());

        Assert.Empty(refusals);
    }

    [Fact]
    public void A_BoneOutsideEitherSkeletonIsRefused()
    {
        var (forward, _) = Vanilla();

        Assert.Contains(SkeletonMapperAuthor.Refusals(forward.SkeletonA, forward.SkeletonB, new[] { (-1, 0) }),
                        refusal => refusal.Contains("not a bone of", StringComparison.Ordinal));
        Assert.Contains(SkeletonMapperAuthor.Refusals(forward.SkeletonA, forward.SkeletonB,
                                                      new[] { (forward.SkeletonA.BoneNames.Count, 0) }),
                        refusal => refusal.Contains("not a bone of", StringComparison.Ordinal));
        Assert.Contains(SkeletonMapperAuthor.Refusals(forward.SkeletonA, forward.SkeletonB, new[] { (0, 18) }),
                        refusal => refusal.Contains("not a bone of", StringComparison.Ordinal));
    }

    [Fact]
    public void AnEmptyPairListIsRefused()
    {
        var (forward, _) = Vanilla();

        Assert.Contains(SkeletonMapperAuthor.Refusals(forward.SkeletonA, forward.SkeletonB, Array.Empty<(int, int)>()),
                        refusal => refusal.Contains("at least one bone pair", StringComparison.Ordinal));
        Assert.Throws<ArgumentException>(() =>
            SkeletonMapperAuthor.FromReferencePose(forward.SkeletonA, forward.SkeletonB, Array.Empty<(int, int)>()));
    }

    private void Compare(IReadOnlyList<HavokRagdollMapping> shipped, IReadOnlyList<HavokRagdollMapping> authored)
    {
        float worstTranslation = 0f;
        float worstRotation = 0f;

        for (int i = 0; i < shipped.Count; i++)
        {
            Assert.Equal(shipped[i].BoneA, authored[i].BoneA);
            Assert.Equal(shipped[i].BoneB, authored[i].BoneB);

            worstTranslation = MathF.Max(worstTranslation,
                Vector3.Distance(shipped[i].AFromBTranslation, authored[i].AFromBTranslation));
            worstRotation = MathF.Max(worstRotation,
                RotationDistance(shipped[i].AFromBRotation, authored[i].AFromBRotation));
        }

        _out.WriteLine($"worst authored translation error {worstTranslation:0.####e+0}, rotation error {worstRotation:0.####e+0} over {shipped.Count} rows");

        Assert.True(worstTranslation < 5e-4f, $"worst translation error {worstTranslation}");
        Assert.True(worstRotation < 1e-5f, $"worst rotation error {worstRotation}");
    }

    private static void AssertClose(Vector3 expected, Vector3 actual, float tolerance, string what) =>
        Assert.True(Vector3.Distance(expected, actual) < tolerance, $"{what}: {expected} against {actual}");

    private static void AssertRotationClose(Quaternion expected, Quaternion actual, float tolerance, string what) =>
        Assert.True(RotationDistance(expected, actual) < tolerance, $"{what}: {expected} against {actual}");

    private static float RotationDistance(Quaternion a, Quaternion b)
    {
        if (a.LengthSquared() < 1e-12f || b.LengthSquared() < 1e-12f) return float.MaxValue;
        a = Quaternion.Normalize(a);
        b = Quaternion.Normalize(b);
        return 2f * MathF.Acos(Math.Clamp(MathF.Abs(Quaternion.Dot(a, b)), -1f, 1f));
    }

    private static (HavokSkeletonMapper Forward, HavokSkeletonMapper Reverse) Vanilla()
    {
        var objects = new PackfileObjects(PackfileImage.Read(FixturePath()));
        var mappers = SkeletonMapperReader.Read(objects);
        Assert.Equal(2, mappers.Count);

        var forward = Assert.Single(mappers, mapper => mapper.SkeletonB.BoneNames.Count == 18);
        var reverse = Assert.Single(mappers, mapper => mapper.SkeletonB.BoneNames.Count == 95);
        return (forward, reverse);
    }

    private static string FixturePath() =>
        Path.Combine(AppContext.BaseDirectory, "fixtures", "vanilla", "Meshes", "Actors", "Character",
                     "CharacterAssets", "skeleton.hkx");
}
