using System.Windows.Controls;
using FO4RecordEditor.Models;
using FO4RecordEditor.ViewModels;

namespace FO4RecordEditor.Views;

public partial class ComparePanel : UserControl
{
    private readonly ShellViewModel _shell;

    public ComparePanel(ShellViewModel shell)
    {
        InitializeComponent();
        _shell = shell;
    }

    public void Show(RecordNode a, RecordNode b) =>
        Grid.ItemsSource = _shell.Diff.Compare(a, b, changesOnly: true);
}
