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
    public ObservableCollection<ChatSession> Sessions { get; } = [];
    private ChatSession _currentSession = null!;
    private readonly FO4RecordEditor.Services.ChatHistoryService _historyService = new();

    public ChatPanel(ShellViewModel shell)
    {
        InitializeComponent();
        _shell = shell;
        Messages.ItemsSource = Bubbles;
        ChatSelector.ItemsSource = Sessions;
        
        var loaded = _historyService.LoadAll();
        foreach (var s in loaded) Sessions.Add(s);
        
        if (Sessions.Count > 0) ChatSelector.SelectedItem = Sessions[0];
        else NewChat_Click(this, new System.Windows.RoutedEventArgs());
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

    private void ChatSelector_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (ChatSelector.SelectedItem is ChatSession session)
        {
            _currentSession = session;
            Bubbles.Clear();
            var aiHistory = new System.Collections.Generic.List<FO4RecordEditor.Models.ChatMessage>();
            foreach (var m in session.Messages)
            {
                Bubbles.Add(new ChatBubble { Text = m.Text, IsUser = m.IsUser });
                aiHistory.Add(new FO4RecordEditor.Models.ChatMessage(m.IsUser ? FO4RecordEditor.Models.ChatRole.User : FO4RecordEditor.Models.ChatRole.Assistant, m.Text));
            }
            _shell.Chat.LoadHistory(aiHistory);
            Scroll.ScrollToEnd();
        }
    }

    private void NewChat_Click(object sender, System.Windows.RoutedEventArgs e)
    {
        var session = new ChatSession();
        Sessions.Insert(0, session);
        ChatSelector.SelectedItem = session;
    }

    /// <summary>Submit a prompt from outside the panel (e.g. AI Actions context menu).</summary>
    public void SubmitExternal(string prompt)
    {
        if (string.IsNullOrWhiteSpace(prompt)) return;
        SendPrompt(prompt);
    }

    private async void SendPrompt(string text)
    {
        if (_currentSession.Messages.Count == 0 && text.Length > 2)
        {
            _currentSession.Name = text.Length > 30 ? text.Substring(0, 30) + "..." : text;
            // Force ComboBox to refresh display
            ChatSelector.Items.Refresh();
        }

        var userMsg = new SessionMessage { IsUser = true, Text = text };
        _currentSession.Messages.Add(userMsg);
        Bubbles.Add(new ChatBubble { Text = text, IsUser = true });
        
        var aiMsg = new SessionMessage { IsUser = false, Text = "" };
        _currentSession.Messages.Add(aiMsg);
        var reply = new ChatBubble { Text = "", IsUser = false };
        Bubbles.Add(reply);

        _historyService.Save(_currentSession);

        try
        {
            var selected = GetSelectedNode();
            
            // Auto-populate lazy-loaded records so the AI gets the full context
            if (selected != null && selected.GetValue("_HasData") == "False")
            {
                var pluginName = selected.Parent?.Parent?.Key ?? "";
                FO4RecordEditor.Services.MutagenLoader.PopulateNode(selected, _shell.GameEnvironment, pluginName);
            }

            var ctx = _shell.Context.BuildForQuestion(text, selected);
            _shell.Log.Log(LogCategory.AI, LogLevel.Info, "Prompt sent", text);

            await _shell.Chat.SendAsync(text, ctx, token =>
                Dispatcher.BeginInvoke(() => { 
                    reply.Text += token; 
                    aiMsg.Text += token;
                    Scroll.ScrollToEnd(); 
                }));
            
            _historyService.Save(_currentSession);
            _shell.Log.Log(LogCategory.AI, LogLevel.Info, "Reply received", reply.Text);
        }
        catch (Exception ex)
        {
            reply.Text = $"[error] {ex.Message}";
            aiMsg.Text = reply.Text;
            _historyService.Save(_currentSession);
            _shell.Log.Log(LogCategory.AI, LogLevel.Error, "AI call failed", ex.Message);
        }
    }

    private RecordNode? GetSelectedNode()
    {
        return _shell.SelectedNode ?? _shell.OpenTabs.LastOrDefault()?.Record;
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
