using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Media;
using FO4RecordEditor.ViewModels;

namespace FO4RecordEditor.Views;

public partial class RecordEditorView : UserControl
{
    public RecordEditorView(RecordTabViewModel tab)
    {
        InitializeComponent();
        
        // Dynamically create columns for each plugin override
        var keys = tab.Record.Values.Keys.Where(k => !string.IsNullOrEmpty(k)).ToList();
        
        // If there are no overrides, fallback to "Value" (which binds to the default Value)
        if (keys.Count == 0)
        {
            keys.Add("");
        }

        int i = 0;
        foreach (var pluginName in keys)
        {
            var header = string.IsNullOrEmpty(pluginName) ? "Value" : pluginName;
            var bindingPath = string.IsNullOrEmpty(pluginName) ? "Node.Value" : $"Node.Values[{pluginName}]";

            // Base plugin = #2D2D24, Winning Override = #24352B
            string bgBrush = (i == keys.Count - 1 && keys.Count > 1) ? "#24352B" : (keys.Count > 1 ? "#2D2D24" : "Transparent");

            var col = new DataGridTemplateColumn
            {
                Header = header,
                Width = new DataGridLength(1, DataGridLengthUnitType.Star)
            };

            // Build a FrameworkElementFactory for a TextBox that fills the cell and sets the background
            var textFactory = new FrameworkElementFactory(typeof(TextBox));
            textFactory.SetBinding(TextBox.TextProperty, new Binding(bindingPath) { Mode = BindingMode.TwoWay, UpdateSourceTrigger = UpdateSourceTrigger.PropertyChanged });
            textFactory.SetValue(TextBox.BackgroundProperty, new BrushConverter().ConvertFromString(bgBrush));
            textFactory.SetValue(TextBox.ForegroundProperty, Brushes.WhiteSmoke);
            textFactory.SetValue(TextBox.BorderThicknessProperty, new Thickness(0));
            textFactory.SetValue(TextBox.PaddingProperty, new Thickness(4, 2, 4, 2));
            textFactory.SetValue(TextBox.FontFamilyProperty, new FontFamily("Consolas"));
            textFactory.SetBinding(TextBox.IsReadOnlyProperty, new Binding("IsCompound"));

            col.CellTemplate = new DataTemplate { VisualTree = textFactory };
            
            // Assign a basic style to remove the DataGridCell default padding/selection artifacts
            var cellStyle = new Style(typeof(DataGridCell));
            cellStyle.Setters.Add(new Setter(DataGridCell.PaddingProperty, new Thickness(0)));
            cellStyle.Setters.Add(new Setter(DataGridCell.BorderThicknessProperty, new Thickness(0)));
            cellStyle.Setters.Add(new Setter(DataGridCell.BackgroundProperty, new BrushConverter().ConvertFromString(bgBrush)));
            cellStyle.Setters.Add(new Setter(DataGridCell.ForegroundProperty, Brushes.WhiteSmoke));
            col.CellStyle = cellStyle;

            FieldList.Columns.Add(col);
            i++;
        }

        FieldList.ItemsSource = tab.FieldsView;
    }
}
