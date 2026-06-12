using System;
using System.Globalization;
using System.Windows.Data;

namespace FO4RecordEditor.Views;

/// <summary>Shows a record field's raw property name as a friendly label (e.g. "Editor ID").
/// One-way; the underlying node key is unchanged so edit paths still resolve.</summary>
public sealed class FriendlyNameConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture) =>
        FO4RecordEditor.Services.Rendering.FriendlyNames.Label(value as string ?? "");

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture) =>
        value;
}
