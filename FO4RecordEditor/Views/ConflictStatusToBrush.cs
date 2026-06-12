using System.Globalization;
using System.Windows.Data;
using System.Windows.Media;
using FO4RecordEditor.Models;

namespace FO4RecordEditor.Views;

/// <summary>Colours tree text by conflict status: winner = orange, loser = red, else default.</summary>
public sealed class ConflictStatusToBrush : IValueConverter
{
    private static readonly Brush Default = new SolidColorBrush(Color.FromRgb(0xCC, 0xCC, 0xCC));
    private static readonly Brush Winner  = new SolidColorBrush(Color.FromRgb(0xD7, 0xBA, 0x7D));
    private static readonly Brush Loser   = new SolidColorBrush(Color.FromRgb(0xF4, 0x47, 0x47));

    static ConflictStatusToBrush()
    {
        Default.Freeze(); Winner.Freeze(); Loser.Freeze();
    }

    public object Convert(object value, Type targetType, object parameter, CultureInfo culture) =>
        value switch
        {
            ConflictStatus.ConflictWinner => Winner,
            ConflictStatus.ConflictLoser  => Loser,
            _ => Default
        };

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture) =>
        throw new NotSupportedException();
}
