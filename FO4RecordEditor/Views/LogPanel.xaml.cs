using System.Windows.Controls;
using FO4RecordEditor.ViewModels;

namespace FO4RecordEditor.Views;

public partial class LogPanel : UserControl
{
    public LogPanel(ShellViewModel shell)
    {
        InitializeComponent();
        Grid.ItemsSource = shell.Log.Entries;
    }
}
