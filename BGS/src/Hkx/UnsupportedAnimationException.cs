using System;

namespace OpenCommonwealth.Services.Hkx;

/// <summary>
/// A real animation class whose decoder is not implemented. Carries the class name so callers can
/// count and group refusals without parsing the message.
/// </summary>
/// <remarks>
/// Derives from <see cref="NotSupportedException"/> on purpose: every existing reader boundary that
/// catches that type keeps working unchanged. <see cref="HkxBinaryReader.TryReadAnimation"/> does not
/// throw it at all — a refusal comes back through the returned data's <c>AnimationClass</c> — so
/// callers that count or group refusals without exceptions read that property, and callers that
/// prefer exceptions catch this type and group by <see cref="AnimationClass"/> rather than parsing
/// the sentence.
/// </remarks>
public sealed class UnsupportedAnimationException : NotSupportedException
{
    public UnsupportedAnimationException(string animationClass, string message) : base(message) =>
        AnimationClass = animationClass ?? "";

    /// <summary>The class name as it is declared in the file, such as hkaQuantizedAnimation.</summary>
    public string AnimationClass { get; }
}
