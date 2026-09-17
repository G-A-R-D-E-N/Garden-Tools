using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;

namespace BehaviourStudio.App;

public sealed record AssistantCommandResult(int ExitCode, string StandardOutput, string StandardError)
{
    public bool Succeeded => ExitCode == 0;
}

public static class AssistantCommandRunner
{
    internal static Func<AssistantCli, IReadOnlyList<string>, TimeSpan, CancellationToken,
        Task<AssistantCommandResult>> RunForTest = DefaultRunAsync;

    public static Task<AssistantCommandResult> RunAsync(
        AssistantCli cli, IReadOnlyList<string> arguments, TimeSpan timeout,
        CancellationToken cancellationToken = default) =>
        RunForTest(cli, arguments, timeout, cancellationToken);

    internal static string ShellPath() =>
        Environment.GetEnvironmentVariable("COMSPEC") is { Length: > 0 } shell ? shell : "cmd.exe";

    private static async Task<AssistantCommandResult> DefaultRunAsync(
        AssistantCli cli, IReadOnlyList<string> arguments, TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var info = new ProcessStartInfo
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        if (cli.NeedsShell)
        {
            info.FileName = ShellPath();
            info.ArgumentList.Add("/c");
            info.ArgumentList.Add(cli.Path);
        }
        else
        {
            info.FileName = cli.Path;
        }
        foreach (string argument in arguments) info.ArgumentList.Add(argument);

        using var process = new Process { StartInfo = info };
        if (!process.Start())
            return new AssistantCommandResult(-1, "", "The command could not be started.");

        using var timeoutSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutSource.CancelAfter(timeout);
        Task<string> output = process.StandardOutput.ReadToEndAsync();
        Task<string> error = process.StandardError.ReadToEndAsync();
        try
        {
            await process.WaitForExitAsync(timeoutSource.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            TryKill(process);
            return new AssistantCommandResult(-1, "", "The command timed out.");
        }

        return new AssistantCommandResult(process.ExitCode,
            await output.ConfigureAwait(false), await error.ConfigureAwait(false));
    }

    private static void TryKill(Process process)
    {
        try
        {
            if (!process.HasExited) process.Kill(true);
        }
        catch (Exception exception) when (exception is InvalidOperationException or NotSupportedException)
        {
        }
    }
}
