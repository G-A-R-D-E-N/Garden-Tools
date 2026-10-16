using System;

namespace OpenCommonwealth.Services.Hkx;

public sealed class UnsupportedAnimationException : NotSupportedException
{
    public UnsupportedAnimationException(string animationClass, string message) : base(message) =>
        AnimationClass = animationClass ?? "";

    public string AnimationClass { get; }
}
