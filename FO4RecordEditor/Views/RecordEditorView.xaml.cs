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

    public RecordEditorView(RecordTabViewModel tab)
    {
        InitializeComponent();

        // One column per plugin that touches the record (xEdit-style). Single plugin = one
        // editable "Value" column; multiple plugins = read-only conflict columns, coloured per cell.
        var plugins = tab.Plugins?.Where(p => !string.IsNullOrEmpty(p)).ToList() ?? new();
        bool conflict = plugins.Count > 1;
        if (plugins.Count == 0) plugins.Add("");   // legacy fallback -> binds Node.Value

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

            var bindingPath = string.IsNullOrEmpty(plugin) ? "Node.Value" : $"Node.Values[{plugin}]";
            var text = new FrameworkElementFactory(typeof(TextBox));
            text.SetBinding(TextBox.TextProperty, new Binding(bindingPath)
            {
                Mode = conflict ? BindingMode.OneWay : BindingMode.TwoWay,
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
            // Read-only for compound (header) rows always, and for every column in conflict view.
            if (conflict)
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
    }

    private static Brush Brush(string hex)
    {
        var b = (SolidColorBrush)new BrushConverter().ConvertFromString(hex)!;
        b.Freeze();
        return b;
    }
}
