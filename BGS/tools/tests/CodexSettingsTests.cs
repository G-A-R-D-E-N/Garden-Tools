using System;
using System.IO;
using System.Threading.Tasks;
using BehaviourStudio.App;
using Microsoft.Extensions.AI;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class CodexSettingsTests
{
    private static readonly string[] CredentialMarkers =
    {
        "api_key", "apikey", "api-key", "token", "secret", "bearer", "password",
        "base_url", "authorization", "cookie", "credential", "auth.json",
    };

    [Fact]
    public async Task AssistantSessionAndSignOutPersistNoCredentialMaterial()
    {
        var tool = AIFunctionFactory.Create(() => new { status = "ok" }, "bgs.inspect_behavior", "test");
        using var fixture = new CodexSessionFixture(new[] { tool });

        await fixture.Session.SendAsync("Check it.", CodexSessionFixture.Context());
        await fixture.Connection.SignOutAsync();
        Settings.Set("assistant.model", "test-model");

        string text = File.Exists(fixture.SettingsPath) ? File.ReadAllText(fixture.SettingsPath) : "";
        Assert.Contains("assistant.model=test-model", text, StringComparison.Ordinal);
        foreach (string marker in CredentialMarkers)
            Assert.DoesNotContain(marker, text, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void ProviderOptionsNeverExposeAnEndpointOrCredentialVariable()
    {
        AssistantProviderOptions options = AssistantProviderOptions.FromSettings();

        Assert.Equal("codex", options.Backend);
        Assert.DoesNotContain("http", options.Model, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("BGS_AI_API_KEY", options.CodexExecutableOverride, StringComparison.Ordinal);
    }

    [Fact]
    public void CodexOverridePathIsPreferredOverPathDiscovery()
    {
        Func<string, bool> previous = CodexLocator.FileExistsForTest;
        Func<System.Collections.Generic.IReadOnlyList<string>> previousCandidates =
            CodexLocator.CandidatePathsForTest;
        try
        {
            CodexLocator.FileExistsForTest = path =>
                string.Equals(path, "configured-codex", StringComparison.Ordinal) ||
                string.Equals(path, "path-codex", StringComparison.Ordinal);
            CodexLocator.CandidatePathsForTest = () => new[] { "path-codex" };

            Assert.True(CodexLocator.TryLocate("configured-codex", out CodexExecutable? executable, out _));
            Assert.Equal("configured-codex", executable!.Path);
            Assert.Equal("configured", executable.Source);

            Assert.True(CodexLocator.TryLocate("", out CodexExecutable? discovered, out _));
            Assert.Equal("path-codex", discovered!.Path);
            Assert.Equal("detected", discovered.Source);
        }
        finally
        {
            CodexLocator.FileExistsForTest = previous;
            CodexLocator.CandidatePathsForTest = previousCandidates;
        }
    }

    [Fact]
    public void MissingCodexExecutableYieldsABoundedUserFacingError()
    {
        Func<string, bool> previous = CodexLocator.FileExistsForTest;
        Func<System.Collections.Generic.IReadOnlyList<string>> previousCandidates =
            CodexLocator.CandidatePathsForTest;
        try
        {
            CodexLocator.FileExistsForTest = _ => false;
            CodexLocator.CandidatePathsForTest = () => new[] { "nope" };

            Assert.False(CodexLocator.TryLocate("", out CodexExecutable? executable, out string error));
            Assert.Null(executable);
            Assert.Contains("Codex CLI was not found", error, StringComparison.Ordinal);
            Assert.True(error.Length < 200);
        }
        finally
        {
            CodexLocator.FileExistsForTest = previous;
            CodexLocator.CandidatePathsForTest = previousCandidates;
        }
    }
}
