using System.Diagnostics;
using System.IO;

namespace FO4RecordEditor.Services;

/// <summary>Outcome of a child-process run. <see cref="Started"/> is false when the process could not be launched.</summary>
public readonly record struct ProcessResult(bool Started, bool TimedOut, int ExitCode, string StdOut, string StdErr)
{
    /// <summary>stdout and stderr joined, CR-stripped and trimmed — the usual thing to show a user.</summary>
    public string Combined => (StdOut + "\n" + StdErr).Replace("\r", "").Trim();
}

/// <summary>
/// Runs a child process and captures both streams without deadlocking.
///
/// The trap this exists to prevent: reading one redirected stream to EOF before the other. A pipe
/// buffer is only a few KB, so once the child fills the stream nobody is draining, it blocks on
/// write — while the parent blocks reading the other stream that will never reach EOF. Neither side
/// moves, and any timeout placed AFTER the reads is unreachable, so it hangs forever rather than
/// failing. Both streams must be drained concurrently, and the drain must start before the wait.
/// </summary>
public static class ProcessRunner
{
    /// <summary>
    /// Starts <paramref name="psi"/>, drains both streams concurrently, and waits up to
    /// <paramref name="timeout"/>, killing the process tree on overrun.
    /// Note: this sets the redirection fields on <paramref name="psi"/> itself.
    /// </summary>
    public static ProcessResult Run(ProcessStartInfo psi, TimeSpan timeout)
    {
        PrepareForCapture(psi);

        using var p = Process.Start(psi);
        if (p == null) return new ProcessResult(false, false, -1, "", "");

        // Start both drains BEFORE waiting — this ordering is the whole point of the class.
        var so = p.StandardOutput.ReadToEndAsync();
        var se = p.StandardError.ReadToEndAsync();

        if (!p.WaitForExit((int)timeout.TotalMilliseconds))
        {
            try { p.Kill(entireProcessTree: true); } catch { }
            DrainRemaining(so, se);
            return new ProcessResult(true, true, -1, TextOf(so), TextOf(se));
        }

        DrainRemaining(so, se);
        return new ProcessResult(true, false, p.ExitCode, TextOf(so), TextOf(se));
    }

    /// <inheritdoc cref="Run(ProcessStartInfo, TimeSpan)"/>
    public static async Task<ProcessResult> RunAsync(
        ProcessStartInfo psi, TimeSpan timeout, CancellationToken ct = default)
    {
        PrepareForCapture(psi);

        using var p = Process.Start(psi);
        if (p == null) return new ProcessResult(false, false, -1, "", "");

        var so = p.StandardOutput.ReadToEndAsync(ct);
        var se = p.StandardError.ReadToEndAsync(ct);

        using var cts = CancellationTokenSource.CreateLinkedTokenSource(ct);
        cts.CancelAfter(timeout);
        try
        {
            await p.WaitForExitAsync(cts.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (!ct.IsCancellationRequested)
        {
            try { p.Kill(entireProcessTree: true); } catch { }
            DrainRemaining(so, se);
            return new ProcessResult(true, true, -1, TextOf(so), TextOf(se));
        }

        DrainRemaining(so, se);
        return new ProcessResult(true, false, p.ExitCode, TextOf(so), TextOf(se));
    }

    private static void PrepareForCapture(ProcessStartInfo psi)
    {
        psi.RedirectStandardOutput = true;
        psi.RedirectStandardError = true;
        psi.UseShellExecute = false;
        psi.CreateNoWindow = true;
    }

    // The process has exited (or been killed), so the readers are finishing the buffered tail.
    // Bounded so a wedged reader cannot turn into the hang this class exists to avoid.
    private static void DrainRemaining(Task<string> so, Task<string> se)
    {
        try { Task.WaitAll(new Task[] { so, se }, 10_000); } catch { }
    }

    private static string TextOf(Task<string> t) => t.IsCompletedSuccessfully ? t.Result : "";
}
