using System;

namespace OpenCommonwealth.Services.Hkx;

/// <summary>
/// One place that explains why an animation's compression is not decoded, so the window, the
/// inspection assistant, the round-trip report and the command line all say the same thing.
/// </summary>
public static class AnimationCompression
{
    /// <summary>The class name Fallout 4 uses for Havok quantized animation (hkaAnimation type 4).</summary>
    public const string QuantizedClass = "hkaQuantizedAnimation";

    /// <summary>The class name Fallout 4 uses for Havok predictive animation (hkaAnimation type 5).</summary>
    public const string PredictiveClass = "hkaPredictiveCompressedAnimation";

    public static bool IsQuantized(string? animationClass) =>
        string.Equals(animationClass, QuantizedClass, StringComparison.Ordinal);

    public static bool IsPredictive(string? animationClass) =>
        string.Equals(animationClass, PredictiveClass, StringComparison.Ordinal);

    /// <summary>
    /// Plain English for an animation class this reader cannot decode. The two compressed kinds are
    /// named because they are what Fallout 4 ships; every other class gets the same sentence with the
    /// class name in it.
    /// </summary>
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

    /// <summary>
    /// Plain English for a spline sub-format this build cannot decode. The reader accepts rotation
    /// quantizations 0, 1, 2 and 5 and the writer produces 1, 2 and 5; anything else is refused
    /// while the file keeps every byte it came with.
    /// </summary>
    public static string DescribeSplineEncoding(int rotationFormat) =>
        $"This animation uses an unsupported spline encoding (rotation quantization format {rotationFormat}), " +
        "which BGS cannot decode yet. The file has not been changed.";

    public static NotSupportedException UnsupportedSplineEncoding(int rotationFormat) =>
        new(DescribeSplineEncoding(rotationFormat));
}
