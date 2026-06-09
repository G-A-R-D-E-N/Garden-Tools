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
        foreach (var path in dlg.FileNames) await _shell.LoadEspAsync(path);
    }

    private async void Scan_Click(object sender, RoutedEventArgs e) => await _shell.RunErrorScanAsync();

    private void Tree_DoubleClick(object sender, MouseButtonEventArgs e)
    {
        if (Tree.SelectedItem is RecordNode n && n.GetValue("FormKey") != null) _open(n);
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
