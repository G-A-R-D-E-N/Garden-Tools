using System.Windows;
using System.Windows.Media;
using Wpf.Ui.Appearance;

namespace FO4RecordEditor;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        // Fluent dark theme + keep the project's teal accent (instead of the system accent).
        ApplicationThemeManager.Apply(
            ApplicationTheme.Dark,
            Wpf.Ui.Controls.WindowBackdropType.Mica,
            updateAccent: false);
        ApplicationAccentColorManager.Apply(
            Color.FromRgb(0x00, 0xA8, 0xA8),   // teal accent
            ApplicationTheme.Dark);
    }
}
