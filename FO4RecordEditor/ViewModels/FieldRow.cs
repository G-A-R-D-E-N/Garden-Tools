using System.ComponentModel;
using System.Runtime.CompilerServices;
using FO4RecordEditor.Models;

namespace FO4RecordEditor.ViewModels;

/// <summary>Flat row shown in the right-panel field editor for a selected record.</summary>
public class FieldRow : INotifyPropertyChanged
{
    private readonly RecordNode _node;
    public int Depth { get; }
    private readonly Action<RecordNode, string>? _editAction;

    public FieldRow(RecordNode node, int depth, Action<RecordNode, string>? editAction = null)
    {
        _node = node;
        Depth = depth;
        _editAction = editAction;
        PropertyChangedEventManager.AddHandler(node, OnNodePropertyChanged, "");
    }

    private void OnNodePropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        Notify(nameof(Value));
        Notify(nameof(IndentedKey));
    }

    public string IndentedKey => new string(' ', Depth * 2) + _node.Key;
    public string FullPath    => BuildPath(_node);
    public bool   IsCompound  => !_node.IsLeaf;

    public string Value
    {
        get => _node.Value;
        set 
        {
            if (_editAction != null)
                _editAction(_node, value);
            else
                _node.Value = value;
        }
    }

    private static string BuildPath(RecordNode n)
    {
        var parts = new List<string>();
        var cur = n;
        while (cur?.Parent != null) { parts.Add(cur.Key); cur = cur.Parent; }
        parts.Reverse();
        return string.Join(".", parts);
    }

    public event PropertyChangedEventHandler? PropertyChanged;
    private void Notify([CallerMemberName] string name = "") =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}
