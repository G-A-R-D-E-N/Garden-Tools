using FO4RecordEditor.Models;
using FO4RecordEditor.Services;

namespace FO4RecordEditor.ViewModels;

public sealed class RecordTabViewModel
{
    public RecordNode Record { get; }
    public string Title { get; }
    private readonly UndoStack _undo;

    public RecordTabViewModel(RecordNode record, UndoStack undo)
    {
        Record = record;
        _undo = undo;
        Title = record.GetValue("EditorID") ?? record.Key;
    }

    public IReadOnlyList<FieldRow> Fields =>
        Record.Children.Select(c => new FieldRow(c, 0, EditField)).ToList();

    /// <summary>Edit a leaf through the undo stack so it is reversible.</summary>
    public void EditField(RecordNode leaf, string newValue)
    {
        if (leaf.Value == newValue) return;
        _undo.Do(new FieldEditChange(leaf, leaf.Value, newValue));
    }
}
