using System.Diagnostics;
using System.IO;

namespace FO4RecordEditor.Services;

public class CompileService
{
    public static async Task<CompileResult> CompileAsync(
        string spriggitExePath,
        string sourceFolder,
        string outputEspPath,
        IProgress<string>? progress = null)
    {
        if (!File.Exists(spriggitExePath))
            return new CompileResult(false, $"Spriggit not found: {spriggitExePath}", 1);

        if (!Directory.Exists(sourceFolder))
            return new CompileResult(false, $"Source folder not found: {sourceFolder}", 1);

        Directory.CreateDirectory(Path.GetDirectoryName(outputEspPath)!);

        var args = $"create-plugin --InputPath \"{sourceFolder}\" --OutputPath \"{outputEspPath}\"";
        progress?.Report($"Running: {spriggitExePath} {args}");

        var psi = new ProcessStartInfo(spriggitExePath, args)
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };

        var output = new System.Text.StringBuilder();
        var error = new System.Text.StringBuilder();

        using var proc = new Process { StartInfo = psi };
        proc.OutputDataReceived += (_, e) => { if (e.Data != null) { output.AppendLine(e.Data); progress?.Report(e.Data); } };
        proc.ErrorDataReceived  += (_, e) => { if (e.Data != null) { error.AppendLine(e.Data);  progress?.Report("[err] " + e.Data); } };

        proc.Start();
        proc.BeginOutputReadLine();
        proc.BeginErrorReadLine();
        await proc.WaitForExitAsync();

        var success = proc.ExitCode == 0;
        var combined = output.ToString() + (error.Length > 0 ? "\n--- stderr ---\n" + error : "");
        return new CompileResult(success, combined, proc.ExitCode);
    }
}

public record CompileResult(bool Success, string Output, int ExitCode);
