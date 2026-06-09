using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using FO4RecordEditor.ViewModels;

namespace FO4RecordEditor;

public partial class MainWindow : Wpf.Ui.Controls.FluentWindow
{
    private readonly ShellViewModel _shell = new();
    private Views.ChatPanel _chatPanel = null!;
    private Views.ExplorerPanel _explorerPanel = null!;

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
        
        _explorerPanel = new Views.ExplorerPanel(_shell, OpenRecordTab, _chatPanel.SubmitExternal);

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

    private void ShowExplorer_Click(object s, RoutedEventArgs e)
    {
        _explorerPanel.StartInSearch = false;
        SidePanelHost.Content = _explorerPanel;
    }
    private void ShowSearch_Click(object s, RoutedEventArgs e)
    {
        _explorerPanel.StartInSearch = true;
        SidePanelHost.Content = _explorerPanel;
    }
    private void ShowErrors_Click(object s, RoutedEventArgs e) => BottomTabs.SelectedIndex = 1;
    private void ShowCompare_Click(object s, RoutedEventArgs e) => BottomTabs.SelectedIndex = 2;
    private void ShowSettings_Click(object s, RoutedEventArgs e) => new Views.SettingsDialog(_shell) { Owner = this }.ShowDialog();

    private async void OpenRecordTab(Models.RecordNode record)
    {
        // Populate the record's fields OFF the UI thread (NPC_ and similar records are huge
        // and froze the UI when walked synchronously). Build detached, then attach here.
        if (record.GetValue("_HasData") == "False")
        {
            SetStatus($"Loading {record.Key}...");
            var env = _shell.GameEnvironment;
            var plugin = record.Parent?.Parent?.Key ?? "";
            var fkStr = record.GetValue("FormKey") ?? "";
            try
            {
                var fields = await Task.Run(() =>
                    Services.MutagenLoader.BuildPopulatedFields(fkStr, env, plugin));

                var flag = record.GetChild("_HasData");
                if (flag != null) record.Children.Remove(flag);
                foreach (var child in fields)
                {
                    // Skip walked duplicates of the FormKey/EditorID/Type header rows.
                    if (record.GetChild(child.Key) != null) continue;
                    child.Parent = record;
                    record.Children.Add(child);
                }
            }
            catch (Exception ex)
            {
                _shell.Log.Log(Models.LogCategory.App, Models.LogLevel.Error, "Record load failed", ex.Message);
            }
            SetStatus("Ready");
        }

        var tab = _shell.OpenRecord(record);
        var ti = new TabItem { Content = new Views.RecordEditorView(tab) };
        ti.Header = BuildClosableHeader(tab.Title, ti, tab);
        CenterTabs.Items.Add(ti);
        CenterTabs.SelectedItem = ti;
        UpdateStatus();
    }

    // Tab header with a close (x) button that removes the tab and its view-model.
    private object BuildClosableHeader(string title, TabItem ti, RecordTabViewModel tab)
    {
        var panel = new StackPanel { Orientation = Orientation.Horizontal };
        panel.Children.Add(new System.Windows.Controls.TextBlock
        {
            Text = title,
            VerticalAlignment = VerticalAlignment.Center,
            Margin = new Thickness(0, 0, 8, 0)
        });
        var close = new Button
        {
            Content = "✕",   // x
            Width = 18,
            Height = 18,
            Padding = new Thickness(0),
            FontSize = 10,
            Cursor = System.Windows.Input.Cursors.Hand,
            ToolTip = "Close tab",
            Background = System.Windows.Media.Brushes.Transparent,
            BorderThickness = new Thickness(0),
            Foreground = System.Windows.Media.Brushes.Gray
        };
        close.Click += (_, _) =>
        {
            CenterTabs.Items.Remove(ti);
            _shell.OpenTabs.Remove(tab);
        };
        panel.Children.Add(close);
        return panel;
    }

    private void UpdateStatus() =>
        StatusLabel.Text = $"{_shell.Graph.RecordCount:N0} records | {_shell.Graph.ReferenceCount:N0} refs | {_shell.Errors.Count} errors";

    public void SetStatus(string text)
    {
        Dispatcher.BeginInvoke(() => StatusLabel.Text = text);
    }

    public void SetProgress(double value)
    {
        Dispatcher.BeginInvoke(() =>
        {
            if (value < 0)
            {
                GlobalProgressBar.Visibility = Visibility.Collapsed;
            }
            else
            {
                GlobalProgressBar.Visibility = Visibility.Visible;
                GlobalProgressBar.IsIndeterminate = value == 0;
                GlobalProgressBar.Value = value;
            }
        });
    }

    private void RegisterCommands()
    {
        var c = _shell.Commands;
        c.Register("open.esp", "Open ESP", "File", () => ShowExplorer_Click(this, null!));
        c.Register("scan.errors", "Run Error Scan", "Errors", async () => await _shell.RunErrorScanAsync());
        c.Register("settings", "Open Settings", "App", () => new Views.SettingsDialog(_shell) { Owner = this }.ShowDialog());
        c.Register("save", "Save Plugin", "File", () => _shell.SaveSelectedPlugin());
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
        else if (e.Key == Key.S && Keyboard.Modifiers == ModifierKeys.Control)
            { _shell.SaveSelectedPlugin(); e.Handled = true; }
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
