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

    public RecordTabViewModel(RecordNode record, UndoStack undo, object? env = null)
    {
        Record = record;
        Undo = undo;
        _env = env;

        if (Record.GetValue("_HasData") == "False")
        {
            var pluginName = Record.Parent?.Parent?.Key ?? "";
            FO4RecordEditor.Services.MutagenLoader.PopulateNode(Record, _env, pluginName);
        }

        Title = record.GetValue("EditorID") ?? record.Key;
        
        AllFields = Flatten(Record, 0, null).ToList();
        
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

    private IEnumerable<FieldRow> Flatten(RecordNode node, int depth, FieldRow? parentRow)
    {
        foreach (var c in node.Children)
        {
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
