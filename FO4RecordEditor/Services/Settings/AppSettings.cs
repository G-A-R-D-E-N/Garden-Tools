namespace FO4RecordEditor.Services;

public sealed class AppSettings
{
    public string AnthropicApiKey { get; set; } = "";
    public string Model { get; set; } = "claude-3-7-sonnet-20250219";
    public string AiProvider { get; set; } = "anthropic";   // anthropic | claudecode | ollama
    public string OllamaUrl { get; set; } = "http://localhost:11434";
    public string OllamaModel { get; set; } = "llama3.1";
    public string ClaudeCodePath { get; set; } = "claude";  // CLI on PATH, or full path
    public string LastFolder { get; set; } = "";
    public string SpriggitExePath { get; set; } = "";
    public string OutputFolder { get; set; } = "";   // where save_plugin writes (blank = <app>/Output)
    public string DataFolder { get; set; } = "";   // Load Env data-folder override (blank = auto-detect / MO2 VFS)
    public string Mo2InstancePath { get; set; } = "";   // MO2 instance folder (the one with mods\ and profiles\)
    public double SidePanelWidth { get; set; } = 320;
    public double AiPanelWidth { get; set; } = 420;
    public double BottomPanelHeight { get; set; } = 200;
}
