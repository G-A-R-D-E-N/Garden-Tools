using FO4RecordEditor.Models;
using FO4RecordEditor.Services;

namespace FO4RecordEditor.ViewModels;

public sealed class RecordTabViewModel
{
    public RecordNode Record { get; }
    public string Title { get; }
    public UndoStack Undo { get; }
    private object? _env;


    public IReadOnlyList<FieldRow> AllFields { get; }
    public System.ComponentModel.ICollectionView FieldsView { get; }

    /// <summary>Plugins that touch this record, in load order (last = winner). One entry = no conflict.</summary>
    public IReadOnlyList<string> Plugins { get; }

    /// <summary>The plugin whose version is currently winning -- the one a patch forwards + edits.</summary>
    public string SourcePlugin =>
        Plugins.Count > 0 ? Plugins[^1] : (Record.Parent?.Parent?.Key ?? "");

    /// <summary>Forward this record into <paramref name="patchPlugin"/> and write the grid edits onto it.</summary>
    public string SaveToPatch(string patchPlugin) =>
        FO4RecordEditor.Services.WriteService.SaveRecordEditsToPatch(_env, Record, SourcePlugin, patchPlugin);

    public string FormKey => Record.GetValue("FormKey") ?? "";

    /// <summary>Copy the winning version into a patch as an override (no field edits required).</summary>
    public string ForwardWinnerToPatch(string patchPlugin) =>
        FO4RecordEditor.Services.WriteService.CopyAsOverride(_env, SourcePlugin, FormKey, patchPlugin);

    /// <summary>Delete the winning plugin's override of this record and save that plugin in place
    /// (xEdit "Remove" — the next plugin down then wins).</summary>
    public string RemoveWinnerOverrideAndSave()
    {
        var del = FO4RecordEditor.Services.WriteService.DeleteRecord(SourcePlugin, FormKey, _env);
        if (!del.StartsWith("Removed", System.StringComparison.OrdinalIgnoreCase)) return del;
        return del + " " + FO4RecordEditor.Services.WriteService.SavePlugin(SourcePlugin, null);
    }

    /// <summary>True when more than one plugin overrides this record (a real conflict set).</summary>
    public bool HasConflicts => Plugins.Count(p => !string.IsNullOrEmpty(p)) > 1;

    /// <summary>The xEdit-style conflict matrix for this record (null if it has no FormKey/env).</summary>
    public Models.ConflictMatrix? BuildConflicts()
    {
        var fk = Record.GetValue("FormKey");
        return string.IsNullOrEmpty(fk)
            ? null
            : FO4RecordEditor.Services.MutagenLoader.BuildConflictMatrix(_env, fk);
    }

    public RecordTabViewModel(RecordNode record, UndoStack undo, object? env = null)
    {
        Record = record;
        Undo = undo;
        _env = env;

        if (Record.GetValue("_HasData") == "False")
            Plugins = FO4RecordEditor.Services.MutagenLoader.PopulateNodeAllVersions(Record, _env);
        else
            Plugins = new[] { Record.Parent?.Parent?.Key ?? "" };

        Title = record.GetValue("EditorID") ?? record.Key;

        AllFields = Flatten(Record, 0, null).ToList();
        foreach (var row in AllFields) row.Plugins = Plugins;
        
        // Setup TreeListView filtering
        FieldsView = System.Windows.Data.CollectionViewSource.GetDefaultView(AllFields);
        FieldsView.Filter = FilterRow;

        foreach (var row in AllFields)
        {
            row.ExpandedChanged += (s, e) => FieldsView.Refresh();
        }
    }

    private bool FilterRow(object item)
    {
        if (item is not FieldRow row) return false;
        var p = row.ParentRow;
        while (p != null)
        {
            if (!p.IsExpanded) return false;
            p = p.ParentRow;
        }
        return true;
    }

    // Identity/metadata children shown in the banner, not as editable field rows.
    private static readonly HashSet<string> _meta = new(StringComparer.OrdinalIgnoreCase)
        { "FormKey", "EditorID", "Type", "_HasData" };

    private IEnumerable<FieldRow> Flatten(RecordNode node, int depth, FieldRow? parentRow)
    {
        foreach (var c in node.Children)
        {
            if (depth == 0 && _meta.Contains(c.Key)) continue;
            var row = new FieldRow(c, depth, parentRow, EditField);
            yield return row;
            foreach (var desc in Flatten(c, depth + 1, row))
                yield return desc;
        }
    }

    /// <summary>Edit a leaf through the undo stack so it is reversible.</summary>
    public void EditField(RecordNode leaf, string newValue)
    {
        if (leaf.Value == newValue) return;
        Undo.Do(new FieldEditChange(leaf, leaf.Value, newValue));
    }
}
