using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;

namespace FO4RecordEditor.Views;

public partial class PluginSelectDialog : Window
{
    public sealed class Item : INotifyPropertyChanged
    {
        private bool _checked;
        public string Name { get; init; } = "";
        public bool IsChecked
        {
            get => _checked;
            set { _checked = value; PropertyChanged?.Invoke(this, new(nameof(IsChecked))); }
        }
        public event PropertyChangedEventHandler? PropertyChanged;
    }

    private readonly ObservableCollection<Item> _items = new();

    public PluginSelectDialog(IEnumerable<string> plugins)
    {
        InitializeComponent();
        foreach (var p in plugins) _items.Add(new Item { Name = p });
        List.ItemsSource = CollectionViewSource.GetDefaultView(_items);
        UpdateCount();
        foreach (var it in _items) it.PropertyChanged += (_, _) => UpdateCount();
    }

    public List<string> SelectedPlugins => _items.Where(i => i.IsChecked).Select(i => i.Name).ToList();

    private void UpdateCount() => CountLabel.Text = $"{_items.Count(i => i.IsChecked)} / {_items.Count} selected";

    private void SelectAll_Click(object sender, RoutedEventArgs e)
    {
        foreach (var it in VisibleItems()) it.IsChecked = true;
    }

    private void ClearAll_Click(object sender, RoutedEventArgs e)
    {
        foreach (var it in VisibleItems()) it.IsChecked = false;
    }

    // Honor the current filter so Select/Clear All apply to what's shown.
    private IEnumerable<Item> VisibleItems()
    {
        var q = FilterBox.Text.Trim();
        return string.IsNullOrEmpty(q)
            ? _items
            : _items.Where(i => i.Name.Contains(q, StringComparison.OrdinalIgnoreCase));
    }

    private void Filter_TextChanged(object sender, TextChangedEventArgs e)
    {
        var view = CollectionViewSource.GetDefaultView(_items);
        var q = FilterBox.Text.Trim();
        view.Filter = string.IsNullOrEmpty(q)
            ? null
            : o => o is Item it && it.Name.Contains(q, StringComparison.OrdinalIgnoreCase);
    }

    private void Ok_Click(object sender, RoutedEventArgs e) { DialogResult = true; }
    private void Cancel_Click(object sender, RoutedEventArgs e) { DialogResult = false; }
}
