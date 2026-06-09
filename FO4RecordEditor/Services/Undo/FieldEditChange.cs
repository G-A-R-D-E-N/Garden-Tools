using FO4RecordEditor.Models;

namespace FO4RecordEditor.Services;

public sealed class FieldEditChange : IChange
{
    private readonly RecordNode _node;
    private readonly string _oldValue;
    private readonly string _newValue;

    public FieldEditChange(RecordNode node, string oldValue, string newValue)
    {
        _node = node; _oldValue = oldValue; _newValue = newValue;
    }

    public string Description => $"Edit {_node.Key}: '{_oldValue}' -> '{_newValue}'";
    public void Apply() => _node.Value = _newValue;
    public void Undo()  => _node.Value = _oldValue;
}
