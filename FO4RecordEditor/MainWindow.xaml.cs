using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using FO4RecordEditor.ViewModels;

namespace FO4RecordEditor;

public partial class MainWindow : Window
{
    private readonly ShellViewModel _shell = new();
    private Views.ChatPanel _chatPanel = null!;

    // Lock objects for cross-thread access to bound collections (integration fix #3).
    private readonly object _logLock = new();
    private readonly object _errorsLock = new();

    public MainWindow()
    {
        InitializeComponent();
        DataContext = _shell;

        // Enable cross-thread writes to collections bound to DataGrids: logging and
        // error scanning happen on background threads during IndexAsync/ScanAsync.
        System.Windows.Data.BindingOperations.EnableCollectionSynchronization(_shell.Log.Entries, _logLock);
        System.Windows.Data.BindingOperations.EnableCollectionSynchronization(_shell.Errors, _errorsLock);

        BottomRow.Height = new GridLength(200);

        _chatPanel = new Views.ChatPanel(_shell);
        AiPanelHost.Content = _chatPanel;

        ShowExplorer_Click(this, null!);            // default side panel
        BuildBottomTabs();
        RegisterCommands();
        UpdateStatus();
    }

    private void BuildBottomTabs()
    {
        BottomTabs.Items.Add(new TabItem
            { Header = "Log", Content = new Views.LogPanel(_shell) });
        BottomTabs.Items.Add(new TabItem
            { Header = "Errors", Content = new Views.ErrorsPanel(_shell) });
        BottomTabs.Items.Add(new TabItem
            { Header = "Compare", Content = new Views.ComparePanel(_shell) });
    }

    private void ShowExplorer_Click(object s, RoutedEventArgs e) =>
        SidePanelHost.Content = new Views.ExplorerPanel(_shell, OpenRecordTab, _chatPanel.SubmitExternal);
    private void ShowSearch_Click(object s, RoutedEventArgs e) =>
        SidePanelHost.Content = new Views.ExplorerPanel(_shell, OpenRecordTab, _chatPanel.SubmitExternal) { StartInSearch = true };
    private void ShowErrors_Click(object s, RoutedEventArgs e) => BottomTabs.SelectedIndex = 1;
    private void ShowCompare_Click(object s, RoutedEventArgs e) => BottomTabs.SelectedIndex = 2;

    private void OpenRecordTab(Models.RecordNode record)
    {
        var tab = _shell.OpenRecord(record);
        var ti = new TabItem { Header = tab.Title, Content = new Views.RecordEditorView(tab) };
        CenterTabs.Items.Add(ti);
        CenterTabs.SelectedItem = ti;
        UpdateStatus();
    }

    private void UpdateStatus() =>
        StatusLabel.Text = $"{_shell.Graph.RecordCount:N0} records | {_shell.Graph.ReferenceCount:N0} refs | {_shell.Errors.Count} errors";

    private void RegisterCommands()
    {
        var c = _shell.Commands;
        c.Register("open.esp", "Open ESP", "File", () => ShowExplorer_Click(this, null!));
        c.Register("scan.errors", "Run Error Scan", "Errors", async () => await _shell.RunErrorScanAsync());
        c.Register("settings", "Open Settings", "App", () => new Views.SettingsDialog(_shell) { Owner = this }.ShowDialog());
        c.Register("undo", "Undo", "Edit", () => _shell.Undo.Undo());
        c.Register("redo", "Redo", "Edit", () => _shell.Undo.Redo());
    }

    private void Window_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.P && Keyboard.Modifiers == (ModifierKeys.Control | ModifierKeys.Shift))
            { TogglePalette(); e.Handled = true; }
        else if (e.Key == Key.Z && Keyboard.Modifiers == ModifierKeys.Control)
            { _shell.Undo.Undo(); e.Handled = true; }
        else if (e.Key == Key.Y && Keyboard.Modifiers == ModifierKeys.Control)
            { _shell.Undo.Redo(); e.Handled = true; }
        else if (e.Key == Key.Escape)
            PaletteOverlay.Visibility = Visibility.Collapsed;
    }

    private void TogglePalette()
    {
        if (PaletteOverlay.Visibility == Visibility.Visible)
            { PaletteOverlay.Visibility = Visibility.Collapsed; return; }
        var palette = new Views.CommandPalette(_shell.Commands);
        palette.Dismiss += () => PaletteOverlay.Visibility = Visibility.Collapsed;
        PaletteHost.Content = palette;
        PaletteOverlay.Visibility = Visibility.Visible;
    }
}
