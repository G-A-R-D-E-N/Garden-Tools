using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using FluentAssertions;
using Xunit;
using Xunit.Abstractions;

namespace FO4RecordEditor.Tests;




public class ShippedDocsTests
{
    private readonly ITestOutputHelper _out;
    public ShippedDocsTests(ITestOutputHelper o) => _out = o;





    private static string RepoRoot()
    {
        var dir = new DirectoryInfo(typeof(ShippedDocsTests).Assembly.Location);
        while (dir != null)
        {
            if (File.Exists(Path.Combine(dir.FullName, "README.md")) &&
                Directory.Exists(Path.Combine(dir.FullName, "FO4RecordEditor")))
                return dir.FullName;
            dir = dir.Parent;
        }
        throw new DirectoryNotFoundException(
            $"Could not locate the repo root above {AppContext.BaseDirectory}.");
    }

    private static string[] ToolNames()
    {
        var directory = Path.Combine(RepoRoot(), "native", "apps", "fo4ide");
        var names = new List<string>();
        var source = File.ReadAllText(Path.Combine(directory, "McpTools.cpp"));
        names.AddRange(Regex.Matches(source, @"\badd\(\s*\{?\s*""([a-z0-9_]+)""|\.name = ""([a-z0-9_]+)""")
            .Select(m => m.Groups[1].Success ? m.Groups[1].Value : m.Groups[2].Value));
        foreach (var catalog in new[] { "McpCatalogRead.cpp", "McpCatalogEdit.cpp", "McpCatalogService.cpp" })
        {
            var text = File.ReadAllText(Path.Combine(directory, catalog));
            names.AddRange(Regex.Matches(text, @"^\s*\{""([a-z0-9_]+)""", RegexOptions.Multiline)
                .Select(m => m.Groups[1].Value));
        }
        var distinct = names.Distinct().ToArray();
        distinct.Should().NotBeEmpty("McpTools.cpp should register the native tools");
        return distinct;
    }

    [Theory]
    [InlineData("README.md", false)]
    [InlineData("docs/MCP_SETUP.md", true)]
    public void StatedToolCountMatchesTheRegisteredTools(string relative, bool mustState)
    {
        var path = Path.Combine(RepoRoot(), relative.Replace('/', Path.DirectorySeparatorChar));
        File.Exists(path).Should().BeTrue($"{relative} should exist at {path}");

        var text = File.ReadAllText(path);
        var expected = ToolNames().Length;





        var counts = Regex.Matches(text, @"\*{0,2}(\d{2,3})\*{0,2}\s+(?:tools|operations)\b")
            .Select(m => int.Parse(m.Groups[1].Value))
            .Distinct()
            .ToList();

        if (mustState)
            counts.Should().NotBeEmpty($"{relative} should state a tool count so this test can guard it");

        foreach (var c in counts) _out.WriteLine($"{relative} states {c}; source has {expected}");
        counts.Where(c => c != expected).Should().BeEmpty(
            $"{relative} states a tool count that no longer matches the registered tools");
    }


    [Fact]
    public void McpSetupDocumentsEveryTool()
    {
        var path = Path.Combine(RepoRoot(), "docs", "MCP_SETUP.md");
        var text = File.ReadAllText(path);

        var missing = ToolNames().Where(n => !text.Contains(n, StringComparison.Ordinal)).ToList();
        if (missing.Count > 0)
            _out.WriteLine("Not documented in docs/MCP_SETUP.md:\n  " + string.Join("\n  ", missing));

        missing.Should().BeEmpty("every tool must appear in the public MCP tool reference");
    }



    [Fact]
    public void McpSetupDoesNotPresentPingAsATool()
    {
        var path = Path.Combine(RepoRoot(), "docs", "MCP_SETUP.md");
        var text = File.ReadAllText(path);

        text.Should().NotContain("call `ping`",
            "ping is a JSON-RPC method, not a tool; point users at list_plugins instead");
    }
}
