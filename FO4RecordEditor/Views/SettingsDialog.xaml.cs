using System.Windows;
using FO4RecordEditor.ViewModels;

namespace FO4RecordEditor.Views;

public partial class SettingsDialog : Window
{
    private readonly ShellViewModel _shell;

    public SettingsDialog(ShellViewModel shell)
    {
        InitializeComponent();
        _shell = shell;
        ApiKeyBox.Password = shell.Settings.Current.AnthropicApiKey;
        ProviderBox.Text = shell.Settings.Current.AiProvider;
        ModelBox.Text = shell.Settings.Current.Model;
    }

    private void Save_Click(object sender, RoutedEventArgs e)
    {
        _shell.Settings.Current.AnthropicApiKey = ApiKeyBox.Password;
        _shell.Settings.Current.AiProvider = ProviderBox.Text;
        _shell.Settings.Current.Model = ModelBox.Text;
        _shell.Settings.Save();
        _shell.RebuildProvider();
        DialogResult = true;
    }
}
