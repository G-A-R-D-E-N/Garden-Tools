using System;
using System.IO;
using System.Linq;
using BehaviourStudio.App;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class AssistantCommandCatalogTests
{
    [Fact]
    public void AProviderWithoutSlashCommandsListsOnlyTheBgsCommands()
    {
        IReadOnlyList<AssistantCommand> commands = AssistantCommandCatalog.For(AssistantBackend.Codex);

        Assert.Equal(AssistantCommands.All.Select(command => command.Name),
            commands.Select(command => command.Name));
    }

    [Fact]
    public void OpencodeCommandsAreDiscoveredFromItsOwnCommandFolder()
    {
        Func<string, IReadOnlyList<AssistantCommand>> previous =
            AssistantCommandCatalog.ProviderCommandsForTest;
        string? asked = null;
        try
        {
            AssistantCommandCatalog.ProviderCommandsForTest = folder =>
            {
                asked = folder;
                return new[] { new AssistantCommand("audit", "Audit the project") };
            };

            IReadOnlyList<AssistantCommand> commands =
                AssistantCommandCatalog.For(AssistantBackend.Opencode);

            Assert.Contains("audit", commands.Select(command => command.Name));
            Assert.Contains("help", commands.Select(command => command.Name));
            Assert.NotNull(asked);
            Assert.Contains("opencode", asked!, StringComparison.OrdinalIgnoreCase);
        }
        finally
        {
            AssistantCommandCatalog.ProviderCommandsForTest = previous;
        }
    }

    [Fact]
    public void CommandFilesBecomeNamesWithTheirFrontmatterDescription()
    {
        string folder = Path.Combine(Path.GetTempPath(), "bgs-cmd-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(folder);
        try
        {
            File.WriteAllText(Path.Combine(folder, "audit.md"),
                "---\ndescription: Audit the project\n---\nbody");
            File.WriteAllText(Path.Combine(folder, "plain.md"), "no frontmatter here");

            IReadOnlyList<AssistantCommand> commands = AssistantCommandCatalog.ReadProviderCommands(folder);

            Assert.Equal(new[] { "audit", "plain" }, commands.Select(command => command.Name));
            Assert.Equal("Audit the project", commands[0].Description);
            Assert.Equal("", commands[1].Description);
        }
        finally
        {
            Directory.Delete(folder, true);
        }
    }

    [Fact]
    public void AMissingCommandFolderYieldsNothingInsteadOfThrowing()
    {
        Assert.Empty(AssistantCommandCatalog.ReadProviderCommands(""));
        Assert.Empty(AssistantCommandCatalog.ReadProviderCommands(
            Path.Combine(Path.GetTempPath(), "bgs-none-" + Guid.NewGuid().ToString("N"))));
    }
}
