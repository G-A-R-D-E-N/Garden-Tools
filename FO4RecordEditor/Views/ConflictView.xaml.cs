using System.Collections.ObjectModel;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using FO4RecordEditor.Models;

namespace FO4RecordEditor.Views;

public partial class ConflictView : Window
{
    private readonly ObservableCollection<ConflictEntry> _all = new();
    private readonly Action<string> _submitToChat;

    public ConflictView(IEnumerable<ConflictEntry> conflicts, Action<string> submitToChat)
    {
        InitializeComponent();
        _submitToChat = submitToChat;
        foreach (var c in conflicts) _all.Add(c);
        Grid.ItemsSource = CollectionViewSource.GetDefaultView(_all);
        ApplyFilter();
    }

    private void Filter_TextChanged(object sender, RoutedEventArgs e) => ApplyFilter();

    private void ApplyFilter()
    {
        if (Grid == null) return;
        var view = CollectionViewSource.GetDefaultView(_all);
        var q = FilterBox.Text.Trim();
        bool modsOnly = ModsOnly.IsChecked == true;

        view.Filter = o =>
        {
            if (o is not ConflictEntry c) return false;
            if (modsOnly && !c.InvolvesMod) return false;
            if (q.Length == 0) return true;
            return c.EditorID.Contains(q, StringComparison.OrdinalIgnoreCase)
                || c.Type.Contains(q, StringComparison.OrdinalIgnoreCase)
                || c.FormKey.Contains(q, StringComparison.OrdinalIgnoreCase)
                || c.PluginsText.Contains(q, StringComparison.OrdinalIgnoreCase);
        };

        int shown = view.Cast<object>().Count();
        CountLabel.Text = $"{shown} shown / {_all.Count} total conflicts";
    }

    private void AskAi_Click(object sender, RoutedEventArgs e)
    {
        var selected = Grid.SelectedItems.OfType<ConflictEntry>().Take(15).ToList();
        if (selected.Count == 0)
        {
            MessageBox.Show("Select one or more conflicts first.");
            return;
        }

        var sb = new StringBuilder();
        sb.AppendLine("These records are in conflict across my loaded plugins. For each, read the " +
            "version in the relevant plugins (use get_record on each), explain what differs, and tell me " +
            "which plugin should win and why (and whether I need a patch):");
        sb.AppendLine();
        foreach (var c in selected)
            sb.AppendLine($"- {c.EditorID} [{c.FormKey}] ({c.Type}) touched by: {c.PluginsText}. Currently wins: {c.Winner}");

        _submitToChat(sb.ToString());
        Close();   // drop the dialog so the chat is visible
    }

    private void Copy_Click(object sender, RoutedEventArgs e)
    {
        var sb = new StringBuilder();
        foreach (var c in CollectionViewSource.GetDefaultView(_all).Cast<ConflictEntry>())
            sb.AppendLine($"{c.EditorID} [{c.FormKey}] ({c.Type})  winner={c.Winner}  <- {c.PluginsText}");
        try { Clipboard.SetText(sb.ToString()); } catch { }
    }

    private void Close_Click(object sender, RoutedEventArgs e) => Close();
}
