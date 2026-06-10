using System.Collections.ObjectModel;
using FO4RecordEditor.Models;
using FO4RecordEditor.Services;

namespace FO4RecordEditor.ViewModels;

public sealed class ShellViewModel
{
    public ObservableCollection<RecordNode> Plugins { get; } = [];
    public ObservableCollection<RecordTabViewModel> OpenTabs { get; } = [];

    public KnowledgeGraph Graph { get; } = new();
    public LogService Log { get; } = new();
    public SettingsService Settings { get; } = new();
    public UndoStack Undo { get; } = new();
    public CommandRegistry Commands { get; } = new();
    public ErrorScanner Scanner { get; } = new();
    public DiffEngine Diff { get; } = new();

    public ChatService Chat { get; private set; } = null!;
    public AIContextBuilder Context { get; private set; } = null!;

    public ObservableCollection<PluginError> Errors { get; } = [];
    public RecordNode? SelectedNode { get; set; }

    public object? GameEnvironment { get; set; }

    // Agentic tool-use client: lets the AI read all loaded plugin data on demand.
    // Null unless the Anthropic provider is selected with an API key.
    public PluginToolExecutor ToolExecutor { get; }
    public AnthropicAgent? Agent { get; private set; }

    // In-process MCP server exposing the plugin tools to the Claude Code CLI, so it gets
    // the same live-plugin tool-use as the Anthropic agent.
    public PluginMcpServer McpServer { get; }

    public ShellViewModel()
    {
        Settings.Load();
        ToolExecutor = new PluginToolExecutor(() => GameEnvironment);
        McpServer = new PluginMcpServer(ToolExecutor);
        McpServer.Start();
        Context = new AIContextBuilder(Graph);
        Chat = new ChatService(CreateProvider());
        BuildAgent();

        // When the AI creates/opens/edits a plugin, show or refresh it in the Explorer tree.
        WriteService.PluginChanged += OnWritePluginChanged;
        WriteService.OutputFolderOverride = Settings.Current.OutputFolder;
        Log.Log(LogCategory.App, LogLevel.Info,
            $"FO4RecordEditor started. Plugin MCP server: {(McpServer.IsRunning ? McpServer.Url : "unavailable")}");
    }

    public void RebuildProvider()
    {
        Chat.SetProvider(CreateProvider());
        BuildAgent();
        WriteService.OutputFolderOverride = Settings.Current.OutputFolder;
    }

    private IAIProvider CreateProvider()
    {
        var s = Settings.Current;
        if (s.AiProvider == "claudecode")
            return new ClaudeCodeProvider(s.ClaudeCodePath,
                McpServer.IsRunning ? McpServer.Url : null, McpServer.ServerName, s.Model);
        return AiProviderFactory.Create(s);
    }

    // Add or refresh an AI-authored plugin in the Explorer tree (marshaled to the UI thread
    // because write tools run on the agent / MCP-server thread).
    private void OnWritePluginChanged(string name)
    {
        var app = System.Windows.Application.Current;
        if (app == null) return;
        app.Dispatcher.Invoke(() =>
        {
            var existing = Plugins.FirstOrDefault(p => string.Equals(p.Key, name, StringComparison.OrdinalIgnoreCase));
            if (existing != null)
            {
                // Reset to a fresh lazy node so newly added records appear on re-expand.
                existing.Children.Clear();
                foreach (var c in MutagenLoader.MakeLazyNode(name, existing.FilePath).Children)
                {
                    c.Parent = existing;
                    existing.Children.Add(c);
                }
                return;
            }
            Plugins.Add(MutagenLoader.MakeLazyNode(name));
        });
    }

    private void BuildAgent()
    {
        var s = Settings.Current;
        Agent = (s.AiProvider == "anthropic" && !string.IsNullOrWhiteSpace(s.AnthropicApiKey))
            ? new AnthropicAgent(s.AnthropicApiKey, s.Model, ToolExecutor)
            : null;
    }

    private bool _isLoadingEsp;
    public async Task LoadEspAsync(string path, IProgress<(string message, double? percent)>? progress = null)
    {
        if (_isLoadingEsp) return;
        _isLoadingEsp = true;
        try
        {
            Log.Log(LogCategory.App, LogLevel.Info, $"Loading {System.IO.Path.GetFileName(path)}...");
            var node = await Task.Run(() => MutagenLoader.LoadEsp(path, progress));
            Plugins.Add(node);
            
            int recCount = 0;
            if (node.Values.TryGetValue("_RecordCount", out var rcStr) && int.TryParse(rcStr, out var parsed))
                recCount = parsed;

            Log.Log(LogCategory.App, LogLevel.Info,
                $"Loaded {node.Key}: ~{recCount} records (lazy loaded).");
        }
        finally
        {
            _isLoadingEsp = false;
        }
    }

    public async Task LoadEnvironmentAsync(IProgress<(string message, double? percent)>? progress = null)
    {
        if (_isLoadingEsp) return;
        _isLoadingEsp = true;
        try
        {
            Log.Log(LogCategory.App, LogLevel.Info, "Loading Load Order Environment...");
            var result = await Task.Run(() => MutagenLoader.LoadEnvironment(progress));
            
            Plugins.Clear();
            Graph.Clear();
            Plugins.Add(result.root);
            GameEnvironment = result.env;
            
            Log.Log(LogCategory.App, LogLevel.Info, "Indexing full environment (this may take a moment)...");
            await Graph.IndexAsync(new[] { result.root },
                new Progress<string>(m => Log.Log(LogCategory.Plugin, LogLevel.Debug, m)));
            Log.Log(LogCategory.App, LogLevel.Info,
                $"Loaded Environment: {Graph.RecordCount} records, {Graph.ReferenceCount} refs.");
        }
        catch (Exception ex)
        {
            Log.Log(LogCategory.App, LogLevel.Error, $"Environment load failed: {ex.Message}");
        }
        finally
        {
            _isLoadingEsp = false;
        }
    }

    private bool _isScanning;
    public async Task RunErrorScanAsync()
    {
        if (_isScanning) return;
        _isScanning = true;
        try
        {
            Log.Log(LogCategory.Error, LogLevel.Info, "Running error scan...");
            var found = await Scanner.ScanAsync(Graph);
            Errors.Clear();
            foreach (var e in found) Errors.Add(e);
            Log.Log(LogCategory.Error, LogLevel.Info, $"Scan complete: {found.Count} issue(s).");
        }
        finally
        {
            _isScanning = false;
        }
    }

    public RecordTabViewModel OpenRecord(RecordNode record)
    {
        var existing = OpenTabs.FirstOrDefault(t => ReferenceEquals(t.Record, record));
        if (existing != null) return existing;
        var tab = new RecordTabViewModel(record, Undo, GameEnvironment);
        OpenTabs.Add(tab);
        return tab;
    }

    public void SaveSelectedPlugin()
    {
        if (SelectedNode == null) return;
        var root = SelectedNode;
        while (root.Parent != null) root = root.Parent;

        if (root.FilePath != null)
        {
            Log.Log(LogCategory.App, LogLevel.Info, $"Saving {root.Key}...");
            var ext = System.IO.Path.GetExtension(root.FilePath).ToLowerInvariant();
            if (ext is ".yaml" or ".yml" or ".json")
                SpriggitLoader.SaveFile(root);
            else if (ext is ".esp" or ".esm" or ".esl")
                MutagenLoader.SaveEsp(root, GameEnvironment!);
            
            Log.Log(LogCategory.App, LogLevel.Info, $"Saved {root.Key}.");
        }
    }
}
