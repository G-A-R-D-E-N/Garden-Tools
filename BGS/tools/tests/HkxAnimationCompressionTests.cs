using System;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class HkxAnimationCompressionTests
{
    private const string Quantized = AnimationCompression.QuantizedClass;
    private const string Predictive = AnimationCompression.PredictiveClass;

    [Fact]
    public void QuantizedCompressionIsExplainedInPlainEnglish()
    {
        Assert.Equal(
            "This animation uses Havok quantized compression (hkaQuantizedAnimation), which BGS cannot " +
            "decode yet. The file has not been changed.",
            AnimationCompression.Describe(Quantized));
    }

    [Fact]
    public void PredictiveCompressionIsExplainedInPlainEnglish()
    {
        Assert.Equal(
            "This animation uses Havok predictive compression (hkaPredictiveCompressedAnimation), which " +
            "BGS cannot decode yet. The file has not been changed.",
            AnimationCompression.Describe(Predictive));
    }

    [Fact]
    public void AnUnknownClassGetsTheGenericSentenceThatNamesIt()
    {
        string text = AnimationCompression.Describe("hkaMysteryAnimation");

        Assert.Contains("hkaMysteryAnimation", text, StringComparison.Ordinal);
        Assert.Contains("cannot decode yet", text, StringComparison.Ordinal);
        Assert.Contains("has not been changed", text, StringComparison.Ordinal);
        Assert.NotEqual(AnimationCompression.Describe(Quantized), text);
    }

    [Fact]
    public void AnEmptyClassSaysTheFileNamesNoAnimation()
    {
        string text = AnimationCompression.Describe("");

        Assert.Contains("has not been changed", text, StringComparison.Ordinal);
        Assert.DoesNotContain("()", text, StringComparison.Ordinal);
    }

    [Fact]
    public void TheTwoCompressedClassesAreRecognisedByExactName()
    {
        Assert.True(AnimationCompression.IsQuantized(Quantized));
        Assert.True(AnimationCompression.IsPredictive(Predictive));
        Assert.False(AnimationCompression.IsQuantized(Predictive));
        Assert.False(AnimationCompression.IsPredictive(Quantized));
        Assert.False(AnimationCompression.IsQuantized(null));
        Assert.False(AnimationCompression.IsPredictive(null));
    }

    [Theory]
    [InlineData(Quantized, "Havok quantized compression")]
    [InlineData(Predictive, "Havok predictive compression")]
    public void TryReadAnimationRefusesTheseClassesAndStillNamesThem(string animationClass, string expected)
    {
        byte[] bytes = CraftedPackfile.Animation(animationClass);

        Assert.False(new HkxBinaryReader().TryReadAnimation(bytes, out var animation));
        Assert.True(animation.HasUnsupportedAnimation);
        Assert.Equal(animationClass, animation.AnimationClass);
        Assert.Contains(expected, AnimationCompression.Describe(animation.AnimationClass), StringComparison.Ordinal);
    }

    [Theory]
    [InlineData(Quantized)]
    [InlineData(Predictive)]
    public void ReadAnimationRefusesWithTheClassifiedMessage(string animationClass)
    {
        byte[] bytes = CraftedPackfile.Animation(animationClass);

        var error = Assert.Throws<UnsupportedAnimationException>(() => new HkxBinaryReader().ReadAnimation(bytes));

        Assert.Equal(animationClass, error.AnimationClass);
        Assert.Equal(AnimationCompression.Describe(animationClass), error.Message);

        // Every boundary that already catches NotSupportedException keeps working unchanged.
        Assert.IsAssignableFrom<NotSupportedException>(error);
    }

    [Fact]
    public void AnUnsupportedSplineEncodingSaysSoAndSaysNothingWasWritten()
    {
        var error = SplineQuat.Unsupported(7);

        Assert.Contains("unsupported spline encoding", error.Message, StringComparison.Ordinal);
        Assert.Contains("7", error.Message, StringComparison.Ordinal);
        Assert.Contains("has not been changed", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AWiderRotationFormatIsRefusedByWidthTheSameWay()
    {
        var error = Assert.Throws<NotSupportedException>(() => SplineFormat.RotWidth(7));

        Assert.Equal(SplineQuat.Unsupported(7).Message, error.Message);
    }

    [Fact]
    public void TheRoundTripReportNamesTheClassAsOneLoss()
    {
        var report = RoundTripReport.ForBytes(CraftedPackfile.Animation(Quantized));

        Assert.Contains(report.Losses, loss =>
            loss.Kind == RoundTripLossKind.UnsupportedAnimation &&
            loss.Message.Contains(Quantized, StringComparison.Ordinal) &&
            loss.Message.Contains("has not been changed", StringComparison.Ordinal));
    }

    [Fact]
    public void ASupportedClassIsStillReadFromTheSameShapeOfFile()
    {
        byte[] bytes = CraftedPackfile.Animation("hkaLosslessCompressedAnimation");

        Assert.True(new HkxBinaryReader().TryReadAnimation(bytes, out var animation));
        Assert.False(animation.HasUnsupportedAnimation);
        Assert.Equal("hkaLosslessCompressedAnimation", animation.AnimationClass);
    }
}
