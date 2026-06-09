using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace FO4RecordEditor.Models;

public class RecordNode : INotifyPropertyChanged
{
    private string _value = "";
    private bool _isDirty;
    private bool _isExpanded;
    private bool _isSelected;

    public string Key { get; init; } = "";
    public string? FilePath { get; set; }
    public RecordNode? Parent { get; set; }
    public ObservableCollection<RecordNode> Children { get; } = [];

    public string Value
    {
        get => _value;
        set
        {
            if (_value == value) return;
            _value = value;
            MarkDirty();
            Notify();
            Notify(nameof(DisplayValue));
        }
    }

    public bool IsDirty
    {
        get => _isDirty;
        private set { _isDirty = value; Notify(); Notify(nameof(DirtyIndicator)); }
    }

    public bool IsExpanded
    {
        get => _isExpanded;
        set { _isExpanded = value; Notify(); }
    }

    public bool IsSelected
    {
        get => _isSelected;
        set { _isSelected = value; Notify(); }
    }

    public bool IsLeaf => Children.Count == 0;
    public string DisplayValue => IsLeaf ? Value : $"({Children.Count} items)";
    public string DirtyIndicator => AnyDirty ? " *" : "";

    public bool AnyDirty => _isDirty || Children.Any(c => c.AnyDirty);

    // ---- helpers used by scripts ----------------------------------------

    public RecordNode? GetChild(string key) =>
        Children.FirstOrDefault(c => string.Equals(c.Key, key, StringComparison.OrdinalIgnoreCase));

    public string? GetValue(string key) => GetChild(key)?.Value;

    public bool SetValue(string key, string value)
    {
        var child = GetChild(key);
        if (child == null) return false;
        child.Value = value;
        return true;
    }

    public IEnumerable<RecordNode> Descendants() =>
        Children.Concat(Children.SelectMany(c => c.Descendants()));

    public IEnumerable<RecordNode> SelfAndDescendants() =>
        Enumerable.Repeat(this, 1).Concat(Descendants());

    // Deep path access: "Conditions[0].Function"
    public RecordNode? Navigate(string path)
    {
        RecordNode? cur = this;
        foreach (var part in path.Split('.'))
        {
            if (cur == null) return null;
            if (part.EndsWith(']'))
            {
                var bracket = part.IndexOf('[');
                var name = part[..bracket];
                var idx = int.Parse(part[(bracket + 1)..^1]);
                cur = string.IsNullOrEmpty(name) ? cur : cur.GetChild(name);
                if (cur == null || idx >= cur.Children.Count) return null;
                cur = cur.Children[idx];
            }
            else
            {
                cur = cur.GetChild(part);
            }
        }
        return cur;
    }

    public string? GetPath(string path) => Navigate(path)?.Value;

    // ---- dirty tracking -------------------------------------------------

    private void MarkDirty()
    {
        IsDirty = true;
        Parent?.BubbleDirty();
    }

    private void BubbleDirty()
    {
        Notify(nameof(DirtyIndicator));
        Parent?.BubbleDirty();
    }

    public void ClearDirty()
    {
        _isDirty = false;
        Notify(nameof(IsDirty));
        Notify(nameof(DirtyIndicator));
        foreach (var c in Children) c.ClearDirty();
    }

    // ---- INotifyPropertyChanged -----------------------------------------

    public event PropertyChangedEventHandler? PropertyChanged;
    private void Notify([CallerMemberName] string name = "") =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}
