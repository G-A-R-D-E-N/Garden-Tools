using System.ComponentModel;
using System.Runtime.CompilerServices;
using FO4RecordEditor.Models;

namespace FO4RecordEditor.ViewModels;

/// <summary>Flat row shown in the right-panel field editor for a selected record.</summary>
public class FieldRow : INotifyPropertyChanged
{
    public RecordNode Node { get; }
    public FieldRow? ParentRow { get; }
    public int Depth { get; }
    private readonly Action<RecordNode, string>? _editAction;

    private bool _isExpanded = true;
    public bool IsExpanded 
    { 
        get => _isExpanded; 
        set 
        { 
            _isExpanded = value; 
            Notify(); 
            // In a real TreeListView, changing this fires an event so the view model refreshes the filter
            ExpandedChanged?.Invoke(this, EventArgs.Empty);
        }
    }
    public event EventHandler? ExpandedChanged;

    public FieldRow(RecordNode node, int depth, FieldRow? parentRow, Action<RecordNode, string>? editAction = null)
    {
        Node = node;
        ParentRow = parentRow;
        Depth = depth;
        _editAction = editAction;
        // Auto-collapse deep nodes (depth >= 1 usually means Arrays or Conditions)
        if (depth > 0) _isExpanded = false;
        
        PropertyChangedEventManager.AddHandler(node, OnNodePropertyChanged, "");
    }

    private void OnNodePropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        Notify(nameof(Value));
        Notify(nameof(Spacer));
    }

    public string Spacer => new string(' ', Depth * 4);
    public string FullPath    => BuildPath(Node);
    public bool   IsCompound  => !Node.IsLeaf;
    public bool   HasChildren => Node.Children.Count > 0;

    public string Value
    {
        get => Node.Value;
        set 
        {
            if (_editAction != null)
                _editAction(Node, value);
            else
                Node.Value = value;
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
