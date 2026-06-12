using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Media;
using FO4RecordEditor.ViewModels;

namespace FO4RecordEditor.Views;

public partial class RecordEditorView : UserControl
{
    private static readonly Brush HeaderMaster = Brush("#B69CFF");  // first / master
    private static readonly Brush HeaderWinner = Brush("#FFB86B");  // last / winner
    private static readonly Brush HeaderMid    = Brush("#D4D4D4");

    private readonly RecordTabViewModel _tab;

    public RecordEditorView(RecordTabViewModel tab)
    {
        InitializeComponent();
        _tab = tab;

        // One column per plugin that touches the record (xEdit-style). Single plugin = one
        // editable "Value" column; multiple plugins = read-only conflict columns, coloured per cell.
        var plugins = tab.Plugins?.Where(p => !string.IsNullOrEmpty(p)).ToList() ?? new();
        bool conflict = plugins.Count > 1;
        if (plugins.Count == 0) plugins.Add("");   // legacy fallback -> binds Node.Value

        BuildBanner(tab, plugins, conflict);

        var cellBrush = (IValueConverter)Resources["CellBrush"];

        for (int i = 0; i < plugins.Count; i++)
        {
            var plugin = plugins[i];
            bool isWinner = i == plugins.Count - 1;
            var header = new TextBlock
            {
                Text = string.IsNullOrEmpty(plugin) ? "Value" : plugin,
                FontFamily = new FontFamily("Consolas"),
                FontWeight = isWinner && conflict ? FontWeights.SemiBold : FontWeights.Normal,
                Foreground = !conflict ? HeaderMid : isWinner ? HeaderWinner : i == 0 ? HeaderMaster : HeaderMid,
            };

            var col = new DataGridTemplateColumn
            {
                Header = header,
                Width = new DataGridLength(1, DataGridLengthUnitType.Star),
            };

            // The winner column is the editable one (its value == Node.Value), so edits there
            // flow into the dirty-tracked tree and can be written to a patch. Other conflict
            // columns are read-only reference views of each plugin's version.
            bool editable = !conflict || isWinner;
            var bindingPath = (editable) ? "Node.Value" : $"Node.Values[{plugin}]";
            var text = new FrameworkElementFactory(typeof(TextBox));
            text.SetBinding(TextBox.TextProperty, new Binding(bindingPath)
            {
                Mode = editable ? BindingMode.TwoWay : BindingMode.OneWay,
                UpdateSourceTrigger = UpdateSourceTrigger.PropertyChanged,
            });
            // Per-cell xEdit colour: bind the row itself, pass the column index as parameter.
            if (conflict)
                text.SetBinding(TextBox.BackgroundProperty, new Binding
                {
                    Converter = cellBrush,
                    ConverterParameter = i.ToString(),
                });
            else
                text.SetValue(TextBox.BackgroundProperty, Brushes.Transparent);

            text.SetValue(TextBox.ForegroundProperty, Brushes.WhiteSmoke);
            text.SetValue(TextBox.BorderThicknessProperty, new Thickness(0));
            text.SetValue(TextBox.PaddingProperty, new Thickness(4, 2, 4, 2));
            text.SetValue(TextBox.FontFamilyProperty, new FontFamily("Consolas"));
            // Compound (header) rows are never editable; non-winner conflict columns are read-only.
            if (!editable)
                text.SetValue(TextBox.IsReadOnlyProperty, true);
            else
                text.SetBinding(TextBox.IsReadOnlyProperty, new Binding("IsCompound"));

            col.CellTemplate = new DataTemplate { VisualTree = text };

            var cellStyle = new Style(typeof(DataGridCell));
            cellStyle.Setters.Add(new Setter(DataGridCell.PaddingProperty, new Thickness(0)));
            cellStyle.Setters.Add(new Setter(DataGridCell.BorderThicknessProperty, new Thickness(0)));
            cellStyle.Setters.Add(new Setter(DataGridCell.BackgroundProperty, Brushes.Transparent));
            cellStyle.Setters.Add(new Setter(DataGridCell.ForegroundProperty, Brushes.WhiteSmoke));
            col.CellStyle = cellStyle;

            FieldList.Columns.Add(col);
        }

        FieldList.ItemsSource = tab.FieldsView;

        // Conflicted records open straight to the Conflicts tab (xEdit-style); otherwise the
        // tab still exists but shows "no conflict". The matrix is built lazily on first view.
        if (tab.HasConflicts) ViewTabs.SelectedItem = ConflictsTab;
    }

    private bool _conflictsBuilt;
    private System.Collections.Generic.IReadOnlyList<Models.ConflictFieldRow> _conflictRows =
        System.Array.Empty<Models.ConflictFieldRow>();

    private void ViewTabs_SelectionChanged(object sender, System.Windows.Controls.SelectionChangedEventArgs e)
    {
        if (e.OriginalSource != ViewTabs) return;   // ignore inner-grid selection bubbling
        if (ReferenceEquals(ViewTabs.SelectedItem, ConflictsTab)) BuildConflictsOnce();
    }

    private void BuildConflictsOnce()
    {
        if (_conflictsBuilt) return;
        _conflictsBuilt = true;

        var matrix = _tab.BuildConflicts();
        if (matrix == null)
        {
            ConflictSummary.Text = "No conflict data for this record.";
            return;
        }

        // Field column + one column per plugin, bound to Values[i] so any number of conflicting
        // plugins (5, 10, …) render — not just a fixed few. Plugin columns get a sensible min
        // width and horizontally scroll when they don't all fit.
        ConflictGrid.Columns.Add(new DataGridTextColumn
        {
            Header = "Field",
            Binding = new System.Windows.Data.Binding(nameof(Models.ConflictFieldRow.Field)),
            Width = 260,
        });
        for (int i = 0; i < matrix.Plugins.Count; i++)
        {
            bool isWinner = i == matrix.Plugins.Count - 1;
            ConflictGrid.Columns.Add(new DataGridTextColumn
            {
                Header = matrix.Plugins[i] + (isWinner && matrix.Plugins.Count > 1 ? "  ◀ winner" : ""),
                Binding = new System.Windows.Data.Binding($"Values[{i}]"),
                Width = new DataGridLength(220),
                MinWidth = 140,
            });
        }

        _conflictRows = matrix.Rows;

        int diffs = 0;
        foreach (var r in matrix.Rows) if (r.Differs) diffs++;
        _diffCount = diffs;
        ConflictSummary.Text = matrix.Plugins.Count > 1
            ? $"{matrix.EditorID} [{matrix.FormKey}] {matrix.Type}   |   {matrix.Plugins.Count} plugins   |   winner: {matrix.Winner}   |   {matrix.Rows.Count} fields, {diffs} differ"
            : $"No conflict — defined only in {matrix.Winner}.";
        if (diffs == 0 && matrix.Rows.Count > 0)
            ConflictSummary.Text += "   (identical override — showing all fields)";
        else if (matrix.Rows.Count == 0)
            ConflictSummary.Text += "   (no expandable fields for this record type)";

        ApplyConflictFilter();
    }

    private int _diffCount;

    private void DiffOnly_Changed(object sender, RoutedEventArgs e) => ApplyConflictFilter();

    private void ApplyConflictFilter()
    {
        if (ConflictGrid == null) return;

        // "Only differing fields" filters to conflicts -- but if NOTHING differs (a benign /
        // identical override), filtering to nothing leaves a blank page. In that case fall back
        // to showing every field so the record is never invisible.
        if (DiffOnly.IsChecked == true && _diffCount > 0)
            ConflictGrid.ItemsSource = _conflictRows.Where(r => r.Differs).ToList();
        else
            ConflictGrid.ItemsSource = _conflictRows;
    }

    // Right-click selects the row under the cursor so the context menu acts on it.
    private static void SelectRowUnderCursor(DataGrid grid, System.Windows.Input.MouseButtonEventArgs e)
    {
        var dep = e.OriginalSource as DependencyObject;
        while (dep != null && dep is not DataGridRow) dep = System.Windows.Media.VisualTreeHelper.GetParent(dep);
        if (dep is DataGridRow row) { row.IsSelected = true; grid.SelectedItem = row.Item; }
    }

    private void ConflictGrid_RightDown(object sender, System.Windows.Input.MouseButtonEventArgs e) =>
        SelectRowUnderCursor(ConflictGrid, e);

    private void FieldGrid_RightDown(object sender, System.Windows.Input.MouseButtonEventArgs e) =>
        SelectRowUnderCursor(FieldList, e);

    private void FieldCopyValue_Click(object sender, RoutedEventArgs e)
    {
        if (FieldList.SelectedItem is FieldRow row) TrySetClipboard(row.Node.Value);
    }

    private void FieldCopyName_Click(object sender, RoutedEventArgs e)
    {
        if (FieldList.SelectedItem is FieldRow row) TrySetClipboard(row.Node.Key);
    }

    private void FieldCopyFormKey_Click(object sender, RoutedEventArgs e) => TrySetClipboard(_tab.FormKey);

    private void ConflictCopyValue_Click(object sender, RoutedEventArgs e)
    {
        if (ConflictGrid.SelectedItem is not Models.ConflictFieldRow row) return;
        // Copy the winning (last) column's value; fall back to the field path if empty.
        var val = row.Values.Count > 0 ? row.Values[^1] : "";
        TrySetClipboard(string.IsNullOrEmpty(val) ? row.Field : val);
    }

    private void ConflictCopyField_Click(object sender, RoutedEventArgs e)
    {
        if (ConflictGrid.SelectedItem is Models.ConflictFieldRow row) TrySetClipboard(row.Field);
    }

    private void ConflictForwardWinner_Click(object sender, RoutedEventArgs e)
    {
        var patch = string.IsNullOrWhiteSpace(PatchName.Text) ? "FallenWorldFixes.esp" : PatchName.Text.Trim();
        ShowConflictStatus(_tab.ForwardWinnerToPatch(patch));
    }

    private void ConflictRemoveOverride_Click(object sender, RoutedEventArgs e)
    {
        var winner = _tab.SourcePlugin;
        var r = MessageBox.Show(
            $"Remove {winner}'s override of this record and save {winner} in place?\n\n" +
            "This edits the plugin file on disk; the next plugin down then wins.",
            "Remove override", MessageBoxButton.YesNo, MessageBoxImage.Warning);
        if (r != MessageBoxResult.Yes) return;
        ShowConflictStatus(_tab.RemoveWinnerOverrideAndSave());
    }

    private void ShowConflictStatus(string msg)
    {
        ConflictSummary.Text = msg;
        ConflictSummary.Foreground = msg.Contains("could not", StringComparison.OrdinalIgnoreCase)
            ? Brush("#E0B060") : Brush("#7FB37F");
    }

    private static void TrySetClipboard(string text)
    {
        if (string.IsNullOrEmpty(text)) return;
        try { Clipboard.SetText(text); } catch { /* clipboard busy */ }
    }

    private void SaveToPatch_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var result = _tab.SaveToPatch(PatchName.Text.Trim());
            PatchStatus.Text = result;
            PatchStatus.Foreground = result.Contains("could not", StringComparison.OrdinalIgnoreCase)
                ? Brush("#E0B060") : Brush("#7FB37F");
        }
        catch (System.Exception ex)
        {
            PatchStatus.Text = $"Save failed: {ex.Message}";
            PatchStatus.Foreground = Brush("#E06060");
        }
    }

    private void BuildBanner(RecordTabViewModel tab, List<string> plugins, bool conflict)
    {
        var fk = tab.Record.GetValue("FormKey") ?? "";
        var eid = tab.Record.GetValue("EditorID") ?? "";
        var type = tab.Record.GetValue("Type") ?? "";
        BannerTitle.Text = $"{(string.IsNullOrEmpty(eid) ? "(no EditorID)" : eid)}   {fk}   {type}";

        if (!conflict)
        {
            var only = plugins.FirstOrDefault(p => !string.IsNullOrEmpty(p));
            BannerStatus.Text = string.IsNullOrEmpty(only)
                ? "No conflict."
                : $"No conflict — defined only in {only}.";
            BannerStatus.Foreground = HeaderMid;
            return;
        }

        int diffs = tab.AllFields.Count(f => f.Differs);
        var winner = plugins[^1];
        BannerStatus.Text = diffs > 0
            ? $"Conflict — {plugins.Count} plugins: {string.Join(" > ", plugins)}   |   winner: {winner}   |   {diffs} field(s) differ"
            : $"Override (no conflict) — {plugins.Count} plugins: {string.Join(" > ", plugins)}   |   identical content";
        BannerStatus.Foreground = diffs > 0 ? HeaderWinner : Brush("#7FB37F");
    }

    private static Brush Brush(string hex)
    {
        var b = (SolidColorBrush)new BrushConverter().ConvertFromString(hex)!;
        b.Freeze();
        return b;
    }
}
