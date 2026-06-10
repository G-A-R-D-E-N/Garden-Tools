using System.Collections.ObjectModel;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using FO4RecordEditor.Models;
using FO4RecordEditor.Services;

namespace FO4RecordEditor.Views;

public partial class ConflictView : Window
{
    private readonly ObservableCollection<ConflictEntry> _all = new();
    private readonly object? _env;
    private readonly Action<string> _submitToChat;
    private readonly Action<string>? _focusInExplorer;

    private ConflictMatrix? _matrix;
    private readonly ObservableCollection<ConflictFieldRow> _rows = new();

    private const int MaxPluginColumns = 8;

    public ConflictView(IEnumerable<ConflictEntry> conflicts, object? env,
        Action<string> submitToChat, Action<string>? focusInExplorer = null)
    {
        InitializeComponent();
        _env = env;
        _submitToChat = submitToChat;
        _focusInExplorer = focusInExplorer;
        foreach (var c in conflicts) _all.Add(c);
        Grid.ItemsSource = CollectionViewSource.GetDefaultView(_all);
        FieldGrid.ItemsSource = CollectionViewSource.GetDefaultView(_rows);
        ApplyFilter();
        RefreshPatchTargets();
    }

    private ConflictEntry? Selected => Grid.SelectedItem as ConflictEntry;

    // ---- left list -------------------------------------------------------

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
        CountLabel.Text = $"{shown} / {_all.Count} conflicts";
    }

    private void Grid_SelectionChanged(object sender, SelectionChangedEventArgs e) => LoadDetail();

    // ---- detail: field matrix -------------------------------------------

    private void LoadDetail()
    {
        _rows.Clear();
        FieldGrid.Columns.Clear();
        var c = Selected;
        if (c == null)
        {
            DetailTitle.Text = "Select a conflict to see what differs";
            DetailSub.Text = "";
            FieldCount.Text = "";
            WinnerCombo.ItemsSource = null;
            return;
        }

        DetailTitle.Text = string.IsNullOrEmpty(c.EditorID) ? c.FormKey : c.EditorID;
        DetailSub.Text = $"{c.Type}  {c.FormKey}";

        _matrix = MutagenLoader.BuildConflictMatrix(_env, c.FormKey);
        if (_matrix == null)
        {
            DetailSub.Text += "   (could not read record versions)";
            return;
        }

        // Field column + one column per plugin (xEdit-style, columns vary per record).
        FieldGrid.Columns.Add(new DataGridTextColumn
        {
            Header = "Field",
            Binding = new Binding(nameof(ConflictFieldRow.Field)),
            Width = new DataGridLength(2, DataGridLengthUnitType.Star),
        });
        int cols = Math.Min(_matrix.Plugins.Count, MaxPluginColumns);
        for (int i = 0; i < cols; i++)
        {
            FieldGrid.Columns.Add(new DataGridTextColumn
            {
                Header = _matrix.Plugins[i],
                Binding = new Binding($"V{i}"),
                Width = new DataGridLength(3, DataGridLengthUnitType.Star),
            });
        }

        // Winner choices = the plugins that touch this record (default to current winner).
        WinnerCombo.ItemsSource = _matrix.Plugins;
        WinnerCombo.SelectedItem = _matrix.Winner;

        ApplyFieldFilter();
    }

    private void DiffOnly_Changed(object sender, RoutedEventArgs e) => ApplyFieldFilter();

    private void ApplyFieldFilter()
    {
        if (_matrix == null) return;
        bool diffOnly = DiffOnly.IsChecked == true;
        _rows.Clear();
        int diffs = 0;
        foreach (var r in _matrix.Rows)
        {
            if (r.Differs) diffs++;
            if (diffOnly && !r.Differs) continue;
            _rows.Add(r);
        }
        FieldCount.Text = $"{diffs} differing / {_matrix.Rows.Count} fields"
            + (_matrix.Plugins.Count > MaxPluginColumns ? $"  (showing first {MaxPluginColumns} of {_matrix.Plugins.Count} plugins)" : "");
    }

    // ---- resolution ------------------------------------------------------

    private void RefreshPatchTargets()
    {
        var targets = new List<string>(WriteService.EditablePlugins());
        const string newPatch = "ConflictPatch.esp";
        if (!targets.Contains(newPatch, StringComparer.OrdinalIgnoreCase)) targets.Add(newPatch);
        var current = PatchCombo.Text;
        PatchCombo.ItemsSource = targets;
        PatchCombo.Text = string.IsNullOrWhiteSpace(current) ? newPatch : current;
    }

    private void Resolve_Click(object sender, RoutedEventArgs e)
    {
        if (_matrix == null) { ResolveStatus.Text = "Select a conflict first."; return; }
        var winner = WinnerCombo.SelectedItem as string ?? _matrix.Winner;
        var patch = PatchCombo.Text?.Trim();
        if (string.IsNullOrWhiteSpace(patch)) { ResolveStatus.Text = "Choose a patch plugin."; return; }

        var msg = WriteService.ResolveConflict(_env, _matrix.FormKey, winner, patch);
        ResolveStatus.Text = msg;
        RefreshPatchTargets();
        PatchCombo.Text = patch;
    }

    private void SavePatch_Click(object sender, RoutedEventArgs e)
    {
        var patch = PatchCombo.Text?.Trim();
        if (string.IsNullOrWhiteSpace(patch)) { ResolveStatus.Text = "Choose a patch plugin to save."; return; }
        ResolveStatus.Text = WriteService.SavePlugin(patch, null);
    }

    // ---- actions ---------------------------------------------------------

    private void AskAi_Click(object sender, RoutedEventArgs e)
    {
        var c = Selected;
        if (c == null) { ResolveStatus.Text = "Select a conflict first."; return; }

        var sb = new StringBuilder();
        sb.AppendLine($"The record {c.EditorID} [{c.FormKey}] ({c.Type}) conflicts across these plugins: " +
            $"{c.PluginsText}. Currently {c.Winner} wins.");
        if (_matrix != null)
        {
            var diffRows = _matrix.Rows.Where(r => r.Differs).Take(40).ToList();
            if (diffRows.Count > 0)
            {
                sb.AppendLine("The fields that actually differ:");
                foreach (var r in diffRows)
                {
                    var parts = _matrix.Plugins
                        .Select((p, i) => $"{p}={(i < r.Values.Count ? r.Values[i] : "")}")
                        .Where(s => !s.EndsWith("="));
                    sb.AppendLine($"  - {r.Field}: {string.Join("  |  ", parts)}");
                }
            }
        }
        sb.AppendLine("Explain what each plugin changes, tell me which should win and why, and whether I need a patch.");
        _submitToChat(sb.ToString());
        Close();
    }

    private void OpenInTree_Click(object sender, RoutedEventArgs e)
    {
        var c = Selected;
        if (c == null) return;
        _focusInExplorer?.Invoke(string.IsNullOrEmpty(c.EditorID) ? c.FormKey : c.EditorID);
    }

    private void CopyFormKey_Click(object sender, RoutedEventArgs e)
    {
        var c = Selected;
        if (c == null) return;
        try { Clipboard.SetText(c.FormKey); } catch { }
    }

    private void Copy_Click(object sender, RoutedEventArgs e)
    {
        var sb = new StringBuilder();
        if (_matrix != null)
        {
            sb.AppendLine($"{_matrix.EditorID} [{_matrix.FormKey}] ({_matrix.Type})  winner={_matrix.Winner}");
            sb.AppendLine("Plugins: " + string.Join(", ", _matrix.Plugins));
            sb.AppendLine();
            foreach (var r in _matrix.Rows.Where(r => r.Differs))
            {
                var parts = _matrix.Plugins.Select((p, i) => $"{p}={(i < r.Values.Count ? r.Values[i] : "")}");
                sb.AppendLine($"{r.Field}\t{string.Join("\t", parts)}");
            }
        }
        else
        {
            foreach (var c in CollectionViewSource.GetDefaultView(_all).Cast<ConflictEntry>())
                sb.AppendLine($"{c.EditorID} [{c.FormKey}] ({c.Type})  winner={c.Winner}  <- {c.PluginsText}");
        }
        try { Clipboard.SetText(sb.ToString()); } catch { }
    }

    private void Close_Click(object sender, RoutedEventArgs e) => Close();
}
