namespace FO4RecordEditor.Services;

public sealed class AppSettings
{
    public string AnthropicApiKey { get; set; } = "";
    public string Model { get; set; } = "claude-3-7-sonnet-20250219";
    public string AiProvider { get; set; } = "anthropic";
    public string OllamaUrl { get; set; } = "http://localhost:11434";
    public string OllamaModel { get; set; } = "llama3.1";
    public string LastFolder { get; set; } = "";
    public string SpriggitExePath { get; set; } = "";
    public double SidePanelWidth { get; set; } = 320;
    public double AiPanelWidth { get; set; } = 420;
    public double BottomPanelHeight { get; set; } = 200;
}
