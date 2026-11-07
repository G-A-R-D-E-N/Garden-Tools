using FluentAssertions;
using FO4RecordEditor.Services;

namespace FO4RecordEditor.Core.Tests;

public class SpaFallbackTests
{
    [Theory]
    [InlineData("GET", "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,*/*;q=0.8", true)]
    [InlineData("GET", "*/*", false)]
    [InlineData("GET", "text/event-stream", false)]
    [InlineData("GET", "application/json", false)]
    [InlineData("POST", "text/html", false)]
    [InlineData("GET", null, false)]
    [InlineData("get", "TEXT/HTML", true)]
    public void IsTopLevelNavigation_OnlyGetWithHtmlAcceptIsNavigation(string method, string? accept, bool expected)
    {
        SpaFallback.IsTopLevelNavigation(method, accept).Should().Be(expected);
    }

    [Fact]
    public void AppEntryPath_MatchesTheUrlTheNativeWindowLoads()
    {
        SpaFallback.AppEntryPath.Should().Be("/#/main");
    }

    [Theory]
    [InlineData("/main", "GET", "text/html", true)]
    [InlineData("/record/000800", "GET", "text/html", true)]
    [InlineData("/", "GET", "text/html", false)]
    [InlineData("", "GET", "text/html", false)]
    [InlineData(null, "GET", "text/html", false)]
    [InlineData("/main", "GET", "*/*", false)]
    [InlineData("/main", "POST", "text/html", false)]
    public void ShouldRedirect_OnlyStrayDeepBrowserNavigationsRedirect(string? path, string method, string accept, bool expected)
    {
        SpaFallback.ShouldRedirect(path, method, accept).Should().Be(expected);
    }
}
