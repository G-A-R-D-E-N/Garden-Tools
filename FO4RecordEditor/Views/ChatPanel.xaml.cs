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
        // Enter sends; Shift+Enter inserts a newline.
        if (e.Key == Key.Enter && (Keyboard.Modifiers & ModifierKeys.Shift) == 0)
        {
            e.Handled = true;
            Send_Click(sender, e);
        }
    }

    private void RenameChat_Click(object sender, System.Windows.RoutedEventArgs e)
    {
        if (_currentSession == null) return;
        var name = Microsoft.VisualBasic.Interaction.InputBox("New chat name:", "Rename Chat", _currentSession.Name);
        if (string.IsNullOrWhiteSpace(name)) return;
        _currentSession.Name = name;
        ChatSelector.Items.Refresh();
        _historyService.Save(_currentSession);
    }

    private void DeleteChat_Click(object sender, System.Windows.RoutedEventArgs e)
    {
        if (_currentSession == null) return;
        var r = System.Windows.MessageBox.Show($"Delete chat '{_currentSession.Name}'?", "Delete Chat",
            System.Windows.MessageBoxButton.YesNo, System.Windows.MessageBoxImage.Warning);
        if (r != System.Windows.MessageBoxResult.Yes) return;

        var toDelete = _currentSession;
        _historyService.Delete(toDelete.Id);
        Sessions.Remove(toDelete);
        if (Sessions.Count > 0) ChatSelector.SelectedItem = Sessions[0];
        else NewChat_Click(this, new System.Windows.RoutedEventArgs());
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

        // Working indicator until the first real token arrives.
        var providerLabel = _shell.Settings.Current.AiProvider switch
        {
            "claudecode" => "Claude Code",
            "ollama"     => "Ollama",
            _            => "Claude"
        };
        var reply = new ChatBubble { Text = $"_{providerLabel} is thinking…_", IsUser = false };
        Bubbles.Add(reply);
        Scroll.ScrollToEnd();

        _historyService.Save(_currentSession);

        bool started = false;
        void Append(string chunk)
        {
            if (!started) { started = true; reply.Text = ""; aiMsg.Text = ""; }
            reply.Text += chunk;
            aiMsg.Text += chunk;
            Scroll.ScrollToEnd();
        }
        void Status(string s) => reply.Text = $"_{s}_";

        try
        {
            var selected = GetSelectedNode();

            // Populate the selected record OFF the UI thread (big records would freeze it).
            if (selected != null && selected.GetValue("_HasData") == "False")
            {
                Dispatcher.Invoke(() => Status($"{providerLabel} is reading {selected.Key}…"));
                var env = _shell.GameEnvironment;
                var plugin = selected.Parent?.Parent?.Key ?? "";
                var fkStr = selected.GetValue("FormKey") ?? "";
                var fields = await Task.Run(() =>
                    FO4RecordEditor.Services.MutagenLoader.BuildPopulatedFields(fkStr, env, plugin));
                var flag = selected.GetChild("_HasData");
                if (flag != null) selected.Children.Remove(flag);
                foreach (var c in fields)
                {
                    if (selected.GetChild(c.Key) != null) continue;
                    c.Parent = selected;
                    selected.Children.Add(c);
                }
            }

            var ctx = _shell.Context.BuildForQuestion(text, selected);
            _shell.Log.Log(LogCategory.AI, LogLevel.Info, "Prompt sent", text);

            if (_shell.Agent != null)
            {
                // Agentic path: the AI can call tools to read any loaded plugin's data.
                var sys = ctx + "\n\n## Tools\nYou have tools to read the user's loaded plugins "
                    + "(list_plugins, list_record_types, list_records, search_records, get_record). "
                    + "Use them to ground every answer in the actual record data. Call list_plugins first "
                    + "if you don't yet know what is loaded.";
                await _shell.Agent.RunAsync(text, sys,
                    onText: chunk => Dispatcher.BeginInvoke(() => Append(chunk)),
                    onToolStatus: status => Dispatcher.BeginInvoke(() =>
                    {
                        if (!started) Status($"Using {status}…");
                        else Append($"\n🔧 {status}\n");
                    }));
            }
            else
            {
                await _shell.Chat.SendAsync(text, ctx,
                    token => Dispatcher.BeginInvoke(() => Append(token)));
            }

            if (!started) Dispatcher.Invoke(() => reply.Text = "_(no response)_");
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
