using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using FO4RecordEditor.Models;
using FO4RecordEditor.ViewModels;
using Microsoft.Win32;

namespace FO4RecordEditor.Views;

public partial class ExplorerPanel : UserControl
{
    private readonly ShellViewModel _shell;
    private readonly Action<RecordNode> _open;
    private readonly Action<string> _submitToChat;
    public bool StartInSearch { get; set; }

    public ExplorerPanel(ShellViewModel shell, Action<RecordNode> open, Action<string> submitToChat)
    {
        InitializeComponent();
        _shell = shell;
        _open = open;
        _submitToChat = submitToChat;
        Tree.ItemsSource = shell.Plugins;
    }


    private async void OpenEsp_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog { Filter = "FO4 Plugins|*.esp;*.esm;*.esl", Multiselect = true };
        if (dlg.ShowDialog() != true) return;

        var main = Window.GetWindow(this) as MainWindow;
        var progress = new Progress<(string message, double? percent)>(p =>
        {
            if (main != null)
            {
                main.SetStatus(p.message);
                if (p.percent.HasValue) main.SetProgress(p.percent.Value);
                else main.SetProgress(-1);
            }
        });

        foreach (var path in dlg.FileNames) await _shell.LoadEspAsync(path);
        if (main != null) main.SetProgress(-1);
    }

    private async void LoadEnv_Click(object sender, RoutedEventArgs e)
    {
        var main = Window.GetWindow(this) as MainWindow;
        var progress = new Progress<(string message, double? percent)>(p =>
        {
            if (main != null)
            {
                main.SetStatus(p.message);
                if (p.percent.HasValue) main.SetProgress(p.percent.Value);
                else main.SetProgress(-1);
            }
        });

        await _shell.LoadEnvironmentAsync(progress);
        if (main != null) main.SetProgress(-1);
    }

    private async void Scan_Click(object sender, RoutedEventArgs e) => await _shell.RunErrorScanAsync();

    private void Tree_DoubleClick(object sender, MouseButtonEventArgs e)
    {
        // Only actual records (nodes with a FormKey) open in a center tab. Group/plugin
        // nodes are left to the TreeView's default double-click expand/collapse, so we no
        // longer open a group's lazy "Loading..." placeholder as a record.
        if (Tree.SelectedItem is RecordNode n && n.GetValue("FormKey") != null)
            _open(n);
    }

    private void Tree_SelectedItemChanged(object sender, RoutedPropertyChangedEventArgs<object> e)
    {
        if (e.NewValue is RecordNode n) _shell.SelectedNode = n;
    }

    private async void Tree_Expanded(object sender, RoutedEventArgs e)
    {
        try
        {
            if (e.OriginalSource is TreeViewItem tvi && tvi.Header is RecordNode node)
            {
                var dummy = node.GetChild("Loading...");
                if (dummy == null) return;

                var needsGroups = dummy.GetValue("_NeedsGroups");
                var needsRecords = dummy.GetValue("_NeedsRecords");
                
                if (needsGroups != null)
                {
                    var main = Window.GetWindow(this) as MainWindow;
                    if (main != null) main.SetProgress(0); // Indeterminate start

                    dummy.Values[""] = "Loading Groups...";
                    var env = _shell.GameEnvironment;
                    var newNodes = await Task.Run(() => 
                    {
                        return FO4RecordEditor.Services.MutagenLoader.GetGroups(needsGroups, env, node, 
                            p => Application.Current.Dispatcher.BeginInvoke(() => {
                                dummy.Values[""] = p;
                                if (main != null) main.SetStatus($"{needsGroups}: {p}");
                            }));
                    });
                    node.Children.Remove(dummy);
                    foreach (var n in newNodes) node.Children.Add(n);
                    if (main != null) main.SetProgress(-1);
                }
                else if (needsRecords != null)
                {
                    var main = Window.GetWindow(this) as MainWindow;
                    if (main != null) main.SetProgress(0); // Indeterminate start

                    dummy.Values[""] = "Loading Records...";
                    var parts = needsRecords.Split('|');
                    if (parts.Length == 2)
                    {
                        var env = _shell.GameEnvironment;
                        var newNodes = await Task.Run(() => 
                        {
                            return FO4RecordEditor.Services.MutagenLoader.GetRecords(parts[0], parts[1], env, node,
                                p => Application.Current.Dispatcher.BeginInvoke(() => {
                                    dummy.Values[""] = p;
                                    if (main != null) main.SetStatus($"{parts[0]} [{parts[1]}]: {p}");
                                }));
                        });
                        node.Children.Remove(dummy);
                        foreach (var n in newNodes) node.Children.Add(n);
                    }
                    if (main != null) main.SetProgress(-1);
                }
            }
        }
        catch (Exception ex)
        {
            _shell.Log.Log(FO4RecordEditor.Models.LogCategory.App, FO4RecordEditor.Models.LogLevel.Error, $"Error expanding tree: {ex.Message}", ex.StackTrace);
        }
    }

    private void Search_TextChanged(object sender, TextChangedEventArgs e)
    {
        var q = SearchBox.Text.Trim();
        if (q.Length < 2) return;
        var hit = _shell.Plugins.SelectMany(p => p.SelfAndDescendants())
            .FirstOrDefault(n => n.Key.Contains(q, StringComparison.OrdinalIgnoreCase)
                              || n.Value.Contains(q, StringComparison.OrdinalIgnoreCase));
        if (hit != null) { var p = hit.Parent; while (p != null) { p.IsExpanded = true; p = p.Parent; } hit.IsSelected = true; }
    }

    private void Tree_ContextMenuOpening(object sender, ContextMenuEventArgs e)
    {
        if (Tree.SelectedItem is RecordNode n && n.GetValue("FormKey") != null)
            Tree.ContextMenu = AiActions.Build(_shell, n, _open, _submitToChat);
        else e.Handled = true;
    }
}
