using System;

namespace OpenCommonwealth.Services.Hkx;

public static class AnimationCompression
{
    public const string QuantizedClass = "hkaQuantizedAnimation";

    public const string PredictiveClass = "hkaPredictiveCompressedAnimation";

    private static bool IsQuantized(string? animationClass) =>
        string.Equals(animationClass, QuantizedClass, StringComparison.Ordinal);

    private static bool IsPredictive(string? animationClass) =>
        string.Equals(animationClass, PredictiveClass, StringComparison.Ordinal);

    public static string Describe(string? animationClass)
    {
        if (IsQuantized(animationClass))
            return "This animation uses Havok quantized compression (hkaQuantizedAnimation), which BGS " +
                   "cannot decode yet. The file has not been changed.";
        if (IsPredictive(animationClass))
            return "This animation uses Havok predictive compression (hkaPredictiveCompressedAnimation), " +
                   "which BGS cannot decode yet. The file has not been changed.";
        if (string.IsNullOrEmpty(animationClass))
            return "This file does not name an animation class BGS can decode. The file has not been changed.";

        return $"This animation uses an animation class BGS cannot decode yet ({animationClass}). " +
               "The file has not been changed.";
    }

    public static string DescribeSplineEncoding(int rotationFormat) =>
        $"This animation uses an unsupported spline sub-format (rotation quantization format {rotationFormat}), " +
        "which BGS cannot decode yet. The file has not been changed.";

    public static string DescribeSplineEncodingForWrite(int rotationFormat) =>
        $"BGS cannot encode the spline sub-format (rotation quantization format {rotationFormat}).";

    public static NotSupportedException UnsupportedSplineEncoding(int rotationFormat) =>
        new(DescribeSplineEncoding(rotationFormat));

    public static NotSupportedException UnsupportedSplineEncodingForWrite(int rotationFormat) =>
        new(DescribeSplineEncodingForWrite(rotationFormat));
}
