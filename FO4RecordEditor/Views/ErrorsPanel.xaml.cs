using System.Windows.Controls;
using FO4RecordEditor.ViewModels;

namespace FO4RecordEditor.Views;

public partial class ErrorsPanel : UserControl
{
    public ErrorsPanel(ShellViewModel shell)
    {
        InitializeComponent();
        Grid.ItemsSource = shell.Errors;
    }
}
