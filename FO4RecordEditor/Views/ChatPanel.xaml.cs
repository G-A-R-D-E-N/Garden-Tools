using System.Collections.ObjectModel;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using FO4RecordEditor.Models;
using FO4RecordEditor.ViewModels;

namespace FO4RecordEditor.Views;

public partial class ChatPanel : UserControl
{
    private readonly ShellViewModel _shell;
    public ObservableCollection<ChatBubble> Bubbles { get; } = [];

    public ChatPanel(ShellViewModel shell)
    {
        InitializeComponent();
        _shell = shell;
        Messages.ItemsSource = Bubbles;
    }

    private void Input_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter && Keyboard.Modifiers == ModifierKeys.Control) { Send_Click(sender, e); e.Handled = true; }
    }

    private void Send_Click(object sender, System.Windows.RoutedEventArgs e)
    {
        var text = Input.Text.Trim();
        if (string.IsNullOrEmpty(text)) return;
        Input.Clear();
        SendPrompt(text);
    }

    /// <summary>Submit a prompt from outside the panel (e.g. AI Actions context menu).</summary>
    public void SubmitExternal(string prompt)
    {
        if (string.IsNullOrWhiteSpace(prompt)) return;
        SendPrompt(prompt);
    }

    private async void SendPrompt(string text)
    {
        Bubbles.Add(new ChatBubble { Text = text, IsUser = true });
        var reply = new ChatBubble { Text = "", IsUser = false };
        Bubbles.Add(reply);

        try
        {
            var selected = GetSelectedRecordEntry();
            var ctx = _shell.Context.BuildForQuestion(text, selected);
            _shell.Log.Log(LogCategory.AI, LogLevel.Info, "Prompt sent", text);

            await _shell.Chat.SendAsync(text, ctx, token =>
                Dispatcher.BeginInvoke(() => { reply.Text += token; Scroll.ScrollToEnd(); }));
            _shell.Log.Log(LogCategory.AI, LogLevel.Info, "Reply received", reply.Text);
        }
        catch (Exception ex)
        {
            reply.Text = $"[error] {ex.Message}";
            _shell.Log.Log(LogCategory.AI, LogLevel.Error, "AI call failed", ex.Message);
        }
    }

    private RecordEntry? GetSelectedRecordEntry()
    {
        var first = _shell.OpenTabs.LastOrDefault();
        return first == null ? null : _shell.Graph.GetByFormKey(first.Record.GetValue("FormKey") ?? "");
    }
}

public sealed class ChatBubble : System.ComponentModel.INotifyPropertyChanged
{
    private string _text = "";
    public bool IsUser { get; set; }
    public string Text { get => _text; set { _text = value; PropertyChanged?.Invoke(this, new(nameof(Text))); } }
    public Brush Bubble => IsUser ? new SolidColorBrush(Color.FromRgb(0x0E, 0x39, 0x5C))
                                  : new SolidColorBrush(Color.FromRgb(0x2A, 0x2A, 0x2A));
    public event System.ComponentModel.PropertyChangedEventHandler? PropertyChanged;
}
