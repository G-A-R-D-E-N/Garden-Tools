using System;
using System.Globalization;
using System.Windows;
using System.Windows.Data;

namespace FO4RecordEditor.Views;

/// <summary>Visible only when the node's FieldEditKind matches the ConverterParameter
/// ("Text" / "Bool" / "Enum"), so one cell template can host a textbox, checkbox, and dropdown
/// and show exactly the right one.</summary>
public sealed class EditKindToVisibility : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture) =>
        string.Equals(value?.ToString(), parameter as string, StringComparison.Ordinal)
            ? Visibility.Visible : Visibility.Collapsed;

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture) =>
        Binding.DoNothing;
}

/// <summary>Bridges a checkbox's bool IsChecked to the node's string value ("True"/"False").</summary>
public sealed class BoolStringConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture) =>
        bool.TryParse(value as string, out var b) && b;

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture) =>
        value is true ? "True" : "False";
}
