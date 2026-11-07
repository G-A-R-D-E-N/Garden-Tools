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
            "This animation uses Havok quantized compression (hkaQuantizedAnimation). BGS decodes only the " +
            "layouts the reverse-engineering record fully specifies, and this file is not one of them. " +
            "The file has not been changed.",
            AnimationCompression.Describe(Quantized));
    }

    [Fact]
    public void PredictiveCompressionIsExplainedInPlainEnglish()
    {
        Assert.Equal(
            "This animation uses Havok predictive compression (hkaPredictiveCompressedAnimation). BGS decodes " +
            "only the layouts the reverse-engineering record fully specifies, and this file is not one of them. " +
            "The file has not been changed.",
            AnimationCompression.Describe(Predictive));
    }

    [Fact]
    public void AnUnknownClassGetsTheGenericSentenceThatNamesIt()
    {
        Assert.Equal(
            "This animation uses an animation class BGS cannot decode yet (hkaMysteryAnimation). " +
            "The file has not been changed.",
            AnimationCompression.Describe("hkaMysteryAnimation"));
    }

    [Fact]
    public void AnEmptyOrMissingClassSaysTheFileNamesNoAnimation()
    {
        string sentence =
            "This file does not name an animation class BGS can decode. The file has not been changed.";

        Assert.Equal(sentence, AnimationCompression.Describe(""));
        Assert.Equal(sentence, AnimationCompression.Describe(null));
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

        Assert.IsAssignableFrom<NotSupportedException>(error);
    }

    [Fact]
    public void AnUnsupportedSplineSubFormatIsExplainedInPlainEnglish()
    {
        var error = SplineQuat.Unsupported(7);

        Assert.Equal(
            "This animation uses an unsupported spline sub-format (rotation quantization format 7), " +
            "which BGS cannot decode yet. The file has not been changed.",
            error.Message);
    }

    [Fact]
    public void AnUnsupportedSplineSubFormatOnAWritePathSaysNothingWasProduced()
    {
        string sentence = "BGS cannot encode the spline sub-format (rotation quantization format 7).";

        Assert.Equal(sentence, AnimationCompression.DescribeSplineEncodingForWrite(7));

        var error = Assert.Throws<NotSupportedException>(
            () => SplineQuat.Write(7, System.Numerics.Quaternion.Identity, new byte[16], 0));

        Assert.Equal(sentence, error.Message);
        Assert.NotEqual(AnimationCompression.DescribeSplineEncoding(7), error.Message);
        Assert.DoesNotContain("has not been changed", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AWiderRotationFormatIsRefusedByWidthWithTheReadWording()
    {
        var error = Assert.Throws<NotSupportedException>(() => SplineFormat.RotWidth(7));

        Assert.Equal(
            "This animation uses an unsupported spline sub-format (rotation quantization format 7), " +
            "which BGS cannot decode yet. The file has not been changed.",
            error.Message);
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
    [Fact]
    public void ADecodedPredictiveClipStaysUnsavableAndTheReportSaysSo()
    {
        byte[] bytes = PredictivePlaybackTests.Fixture(31, 1, false);
        var animation = new HkxBinaryReader().ReadAnimation(bytes);

        var refused = Assert.Throws<InvalidOperationException>(() => NativeAnimation.Recompress(bytes, animation));
        Assert.Contains("saving it is not supported yet", refused.Message, StringComparison.Ordinal);

        var loss = Assert.Single(RoundTripReport.ForBytes(bytes).Losses);
        Assert.Equal(RoundTripLossKind.UnsupportedAnimation, loss.Kind);
        Assert.Equal(refused.Message, loss.Message);
    }

}
