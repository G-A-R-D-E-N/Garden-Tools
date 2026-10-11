using BehaviourStudio.App;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class AssistantOpenUrlTests
{
    [Theory]
    [InlineData("https://example.com/login?state=abc")]
    [InlineData("http://localhost:8080/callback")]
    [InlineData("HTTPS://EXAMPLE.COM")]
    public void WebUrlsAreLaunchable(string url) => Assert.True(AssistantUi.IsWebUrl(url));

    [Theory]
    [InlineData("HTTPS://EXAMPLE.COM", "https://example.com/")]
    [InlineData("https://example.com/a b", "https://example.com/a%20b")]
    public void LaunchedUrlIsTheNormalisedUri(string url, string expected) =>
        Assert.Equal(expected, AssistantUi.WebUrlToOpen(url));

    [Theory]
    [InlineData("file:///C:/Windows/System32/calc.exe")]
    [InlineData("calc.exe")]
    [InlineData("ms-settings:")]
    [InlineData("javascript:alert(1)")]
    [InlineData("foo/bar")]
    [InlineData("")]
    [InlineData("   ")]
    [InlineData(null)]
    public void NonWebUrlsAreNotLaunched(string? url) => Assert.False(AssistantUi.IsWebUrl(url));
}
