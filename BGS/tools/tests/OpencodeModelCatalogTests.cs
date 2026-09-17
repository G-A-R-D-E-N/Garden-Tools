using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using BehaviourStudio.App;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class OpencodeModelCatalogTests
{
    [Fact]
    public void ParsingKeepsValidIdsInOrderAndDropsAnythingElse()
    {
        const string output =
            "opencode-go/gpt-5.6-luna\r\n" +
            "\n" +
            "  anthropic/claude-haiku-4-5  \n" +
            "opencode-go/gpt-5.6-luna\n" +
            "not-a-model\n" +
            "trailing/\n" +
            "/leading\n" +
            "has space/model\n" +
            "a/b/c\n" +
            "opencode/big-pickle\n";

        IReadOnlyList<CodexModel> models = OpencodeModelCatalog.Parse(output);

        Assert.Equal(
            new[] { "opencode-go/gpt-5.6-luna", "anthropic/claude-haiku-4-5", "opencode/big-pickle" },
            models.Select(model => model.WireModel));
        Assert.All(models, model => Assert.Equal(model.WireModel, model.DisplayName));
    }

    [Fact]
    public void AnEmptyCatalogParsesToNothingInsteadOfThrowing()
    {
        Assert.Empty(OpencodeModelCatalog.Parse(""));
        Assert.Empty(OpencodeModelCatalog.Parse("\n\n\r\n"));
    }

    [Fact]
    public async Task ReadingUsesTheCliCatalogRatherThanABuiltInList()
    {
        Func<AssistantCli, IReadOnlyList<string>, IReadOnlyDictionary<string, string>?, TimeSpan,
            CancellationToken, Task<AssistantCommandResult>> previous = AssistantCommandRunner.RunForTest;
        try
        {
            IReadOnlyList<string>? arguments = null;
            AssistantCommandRunner.RunForTest = (_, args, _, _, _) =>
            {
                arguments = args;
                return Task.FromResult(new AssistantCommandResult(0,
                    "opencode-go/gpt-5.6-luna\nanthropic/claude-haiku-4-5\n", ""));
            };

            IReadOnlyList<CodexModel> models = await OpencodeModelCatalog
                .ReadAsync(new AssistantCli("opencode", "detected"));

            Assert.Equal(
                new[] { "opencode-go/gpt-5.6-luna", "anthropic/claude-haiku-4-5" },
                models.Select(model => model.WireModel));
            Assert.Equal(new[] { "models" }, arguments);
        }
        finally
        {
            AssistantCommandRunner.RunForTest = previous;
        }
    }

    [Fact]
    public async Task AFailedCatalogCommandYieldsNoModelsRatherThanGuesses()
    {
        Func<AssistantCli, IReadOnlyList<string>, IReadOnlyDictionary<string, string>?, TimeSpan,
            CancellationToken, Task<AssistantCommandResult>> previous = AssistantCommandRunner.RunForTest;
        try
        {
            AssistantCommandRunner.RunForTest = (_, _, _, _, _) =>
                Task.FromResult(new AssistantCommandResult(1, "", "failed"));

            Assert.Empty(await OpencodeModelCatalog.ReadAsync(new AssistantCli("opencode", "detected")));
        }
        finally
        {
            AssistantCommandRunner.RunForTest = previous;
        }
    }

    [Fact]
    public void WindowsCommandShimsRunThroughTheShellAndOtherPathsDoNot()
    {
        Assert.True(new AssistantCli("claude.cmd", "detected").NeedsShell);
        Assert.False(new AssistantCli("opencode", "detected").NeedsShell);
        Assert.NotEmpty(AssistantCommandRunner.ShellPath());
    }
}
