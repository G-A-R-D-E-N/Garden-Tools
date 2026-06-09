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

    public ShellViewModel()
    {
        Settings.Load();
        Chat = new ChatService(AiProviderFactory.Create(Settings.Current));
        Context = new AIContextBuilder(Graph);
        Log.Log(LogCategory.App, LogLevel.Info, "FO4RecordEditor started.");
    }

    public void RebuildProvider() => Chat.SetProvider(AiProviderFactory.Create(Settings.Current));

    private bool _isLoadingEsp;
    public async Task LoadEspAsync(string path)
    {
        if (_isLoadingEsp) return;
        _isLoadingEsp = true;
        try
        {
            Log.Log(LogCategory.App, LogLevel.Info, $"Loading {System.IO.Path.GetFileName(path)}...");
            var node = await Task.Run(() => MutagenLoader.LoadEsp(path));
            Plugins.Add(node);
            await Graph.IndexAsync(new[] { node },
                new Progress<string>(m => Log.Log(LogCategory.Plugin, LogLevel.Debug, m)));
            Log.Log(LogCategory.App, LogLevel.Info,
                $"Loaded {node.Key}: {Graph.RecordCount} records, {Graph.ReferenceCount} refs.");
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
        var tab = new RecordTabViewModel(record, Undo);
        OpenTabs.Add(tab);
        return tab;
    }
}
