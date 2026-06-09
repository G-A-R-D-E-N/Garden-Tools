using System.Diagnostics;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text;
using FO4RecordEditor.Models;

namespace FO4RecordEditor.Services;

/// <summary>
/// AI backend that drives the local Claude Code CLI (`claude -p`) instead of the
/// Anthropic HTTP API, so the user can use their Claude Code sign-in with no API key.
/// The conversation is flattened into a single print-mode prompt fed via stdin.
/// </summary>
public sealed class ClaudeCodeProvider : IAIProvider
{
    private readonly string _exePath;
    private readonly string? _mcpUrl;
    private readonly string _mcpServerName;
    private readonly string? _model;

    public string Name => "Claude Code";

    public ClaudeCodeProvider(string exePath = "claude", string? mcpUrl = null,
        string mcpServerName = "fo4editor", string? model = null)
    {
        _exePath = string.IsNullOrWhiteSpace(exePath) ? "claude" : exePath;
        _mcpUrl = mcpUrl;
        _mcpServerName = mcpServerName;
        _model = string.IsNullOrWhiteSpace(model) ? null : model;
    }

    public async IAsyncEnumerable<string> StreamAsync(
        IReadOnlyList<ChatMessage> messages,
        [EnumeratorCancellation] CancellationToken ct = default)
    {
        var prompt = new StringBuilder();
        if (_mcpUrl != null)
        {
            prompt.AppendLine(
                $"You have MCP tools to read the user's loaded Bethesda plugins: " +
                $"mcp__{_mcpServerName}__list_plugins, list_record_types, list_records, " +
                $"search_records, and get_record. Use them to ground every answer in the actual " +
                $"record data. Call list_plugins first if you don't know what is loaded.\n");
        }
        foreach (var m in messages)
        {
            switch (m.Role)
            {
                case ChatRole.System:    prompt.AppendLine(m.Content).AppendLine(); break;
                case ChatRole.User:      prompt.AppendLine("User: " + m.Content); break;
                case ChatRole.Assistant: prompt.AppendLine("Assistant: " + m.Content); break;
            }
        }

        // Route through cmd.exe so npm shims (claude.cmd / claude.ps1) resolve via PATHEXT --
        // Process.Start cannot launch a .cmd/.ps1 directly on Windows.
        var psi = new ProcessStartInfo
        {
            FileName = "cmd.exe",
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
            WorkingDirectory = Path.GetTempPath(),
            // claude emits UTF-8; without this the OEM code page mangles characters
            // like the em dash into "ΓÇö".
            StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8,
            StandardInputEncoding = Encoding.UTF8,
        };
        psi.ArgumentList.Add("/c");
        psi.ArgumentList.Add(_exePath);                   // "claude" or a full path
        psi.ArgumentList.Add("-p");                       // print (non-interactive) mode
        psi.ArgumentList.Add("--output-format");
        psi.ArgumentList.Add("text");
        if (_model != null)
        {
            psi.ArgumentList.Add("--model");
            psi.ArgumentList.Add(_model);
        }

        // Register the in-process plugin MCP server so Claude Code can read live plugin data.
        if (_mcpUrl != null)
        {
            var cfg = $"{{\"mcpServers\":{{\"{_mcpServerName}\":{{\"type\":\"http\",\"url\":\"{_mcpUrl}\"}}}}}}";
            psi.ArgumentList.Add("--mcp-config");
            psi.ArgumentList.Add(cfg);
            psi.ArgumentList.Add("--allowedTools");
            psi.ArgumentList.Add($"mcp__{_mcpServerName}");   // auto-approve this server's tools in -p mode
        }

        Process? proc = null;
        string? startError = null;
        try
        {
            proc = Process.Start(psi);
        }
        catch (Exception ex)
        {
            startError = ex.Message;
        }

        if (proc == null)
        {
            yield return $"[Claude Code not available: {startError}. " +
                         "Install the Claude Code CLI and ensure `claude` is on your PATH, " +
                         "or set its full path in Settings.]";
            yield break;
        }

        using (proc)
        {
            // Feed the prompt via stdin, then close so the CLI starts generating.
            await proc.StandardInput.WriteAsync(prompt.ToString());
            proc.StandardInput.Close();

            string? line;
            while ((line = await proc.StandardOutput.ReadLineAsync(ct)) != null)
                yield return line + "\n";

            await proc.WaitForExitAsync(ct);

            if (proc.ExitCode != 0)
            {
                var err = await proc.StandardError.ReadToEndAsync(ct);
                if (!string.IsNullOrWhiteSpace(err))
                    yield return $"\n[Claude Code error] {err.Trim()}";
            }
        }
    }
}
