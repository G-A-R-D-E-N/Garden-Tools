using BehaviourStudio.App;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class RenderingModeTests
{
    [Fact]
    public void TheDefaultOrderPrefersAngleThenWglThenSoftware()
    {
        Assert.Equal("BGS_RENDERING", Program.RenderingPreferenceVariable);
        Assert.Equal(new[] { "AngleEgl", "Wgl", "Software" }, Program.RenderingModeOrderFor(null));
        Assert.Equal(new[] { "AngleEgl", "Wgl", "Software" }, Program.RenderingModeOrderFor(""));
        Assert.Equal(new[] { "AngleEgl", "Wgl", "Software" }, Program.RenderingModeOrderFor("  "));
    }

    [Fact]
    public void TheOptInOverrideMovesItsModeToTheFrontAndKeepsTheRestAsFallbacks()
    {
        Assert.Equal(new[] { "Wgl", "AngleEgl", "Software" }, Program.RenderingModeOrderFor("wgl"));
        Assert.Equal(new[] { "Wgl", "AngleEgl", "Software" }, Program.RenderingModeOrderFor(" WGL "));
        Assert.Equal(new[] { "Software", "AngleEgl", "Wgl" }, Program.RenderingModeOrderFor("software"));
        Assert.Equal(new[] { "AngleEgl", "Wgl", "Software" }, Program.RenderingModeOrderFor("angle"));
    }

    [Fact]
    public void AnUnknownPreferenceLeavesTheDefaultOrderAlone()
    {
        Assert.Equal(new[] { "AngleEgl", "Wgl", "Software" }, Program.RenderingModeOrderFor("vulkan"));
        Assert.Same(Program.DefaultRenderingModes, Program.RenderingModeOrderFor("vulkan"));
    }
}
