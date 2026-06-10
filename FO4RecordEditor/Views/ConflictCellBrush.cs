using System.Globalization;
using System.Windows.Data;
using System.Windows.Media;
using FO4RecordEditor.ViewModels;

namespace FO4RecordEditor.Views;

/// <summary>
/// Per-cell background for the record viewport's plugin columns, following xEdit's conflict model.
/// Bound with the FieldRow as the value and the plugin's column index as ConverterParameter.
///   identical across all plugins -> green   (benign / identical-to-master)
///   addition present in one plugin only -> yellow   (override, no conflict)
///   master (first column) of a differing field -> purple
///   winner (last column with a value) of a differing field -> orange
///   any other differing cell -> red   (conflict loser)
/// </summary>
public sealed class ConflictCellBrush : IValueConverter
{
    private static SolidColorBrush B(string hex)
    {
        var b = (SolidColorBrush)new BrushConverter().ConvertFromString(hex)!;
        b.Freeze();
        return b;
    }

    private static readonly Brush Transparent = Brushes.Transparent;
    private static readonly Brush Empty   = B("#181818");  // field not defined in this plugin
    private static readonly Brush Green   = B("#1F3A24");  // identical across plugins
    private static readonly Brush Yellow  = B("#3D3A18");  // override / addition, no conflict
    private static readonly Brush Orange  = B("#4A3618");  // conflict winner
    private static readonly Brush Purple  = B("#2E2340");  // master / earliest
    private static readonly Brush Red     = B("#3E2020");  // conflict loser

    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is not FieldRow row || !int.TryParse(parameter?.ToString(), out var i))
            return Transparent;

        var plugins = row.Plugins;
        if (plugins.Count < 2 || i >= plugins.Count) return Transparent;
        if (!row.Node.IsLeaf) return Transparent;   // compound headers: no fill

        var vals = plugins.Select(row.ValueFor).ToList();
        var present = vals.Where(s => s.Length > 0).ToList();
        if (present.Count == 0) return Transparent;

        bool differs = present.Distinct().Count() > 1 || present.Count < plugins.Count;
        var me = vals[i];

        if (!differs) return Green;          // every plugin agrees
        if (me.Length == 0) return Empty;    // this plugin doesn't define the field
        if (present.Count == 1) return Yellow;

        int winner = vals.FindLastIndex(v => v.Length > 0);
        if (i == winner) return Orange;
        if (i == 0) return Purple;
        return Red;
    }

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
        => Binding.DoNothing;
}
