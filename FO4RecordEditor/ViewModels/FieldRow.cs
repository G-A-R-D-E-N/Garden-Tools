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

    // xEdit-style label: a list item reads as the singular element name ("Component", "Condition")
    // instead of "[0]"; every other field gets its friendly label ("Editor ID", "Marker Color").
    public string DisplayLabel
    {
        get
        {
            var key = Node.Key;
            if (key.Length > 1 && key[0] == '[' && ParentRow != null)
                return Singular(ParentRow.Node.Key);
            return FO4RecordEditor.Services.Rendering.FriendlyNames.Label(key);
        }
    }

    private static string Singular(string plural)
    {
        if (plural.EndsWith("ies", System.StringComparison.Ordinal)) return plural[..^3] + "y";
        if (plural.Length > 1 && plural.EndsWith("s", System.StringComparison.Ordinal)) return plural[..^1];
        return plural;
    }

    // ---- conflict (xEdit-style) ----------------------------------------------
    // Plugins that touch the owning record, in load order. One entry = no conflict context.
    public IReadOnlyList<string> Plugins { get; set; } = System.Array.Empty<string>();

    /// <summary>This field's value in a specific plugin's version ("" if not set there).</summary>
    public string ValueFor(string plugin) => Node.Values.TryGetValue(plugin, out var v) ? v : "";

    /// <summary>True when this leaf's value is not the same across all plugins (a real conflict).</summary>
    public bool Differs
    {
        get
        {
            // A summary node (a condition/component shown as one line) keeps children but should
            // still colour as a conflict when its one-line value differs across plugins.
            if (Plugins.Count < 2 || (!Node.IsLeaf && !Node.IsSummary)) return false;
            var present = Plugins.Select(ValueFor).Where(s => s.Length > 0).ToList();
            if (present.Count == 0) return false;
            return present.Distinct().Count() > 1 || present.Count < Plugins.Count;
        }
    }

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
