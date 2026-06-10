using System.Windows;

namespace FO4RecordEditor.Views;

public partial class ResultsDialog : Window
{
    public ResultsDialog(string title, string body)
    {
        InitializeComponent();
        Title = title;
        HeaderLabel.Text = title;
        Body.Text = body;
    }

    public static void Show(Window? owner, string title, string body)
    {
        var dlg = new ResultsDialog(title, body) { Owner = owner };
        dlg.Show();
    }

    private void Copy_Click(object sender, RoutedEventArgs e)
    {
        try { Clipboard.SetText(Body.Text); } catch { }
    }

    private void Close_Click(object sender, RoutedEventArgs e) => Close();
}
