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

    // Live activity feedback for the status strip (spinner + state + elapsed).
    private readonly System.Windows.Threading.DispatcherTimer _statusTimer;
    private DateTime _sendStart;

    public ChatPanel(ShellViewModel shell)
    {
        InitializeComponent();
        _shell = shell;
        _statusTimer = new System.Windows.Threading.DispatcherTimer
            { Interval = TimeSpan.FromMilliseconds(500) };
        _statusTimer.Tick += (_, _) =>
            ElapsedText.Text = $"{(DateTime.Now - _sendStart).TotalSeconds:0}s";
        Messages.ItemsSource = Bubbles;
        ChatSelector.ItemsSource = Sessions;
        CommandList.ItemsSource = CommandSuggestions;
        
        var loaded = _historyService.LoadAll();
        foreach (var s in loaded) Sessions.Add(s);
        
        if (Sessions.Count > 0) ChatSelector.SelectedItem = Sessions[0];
        else NewChat_Click(this, new System.Windows.RoutedEventArgs());
    }

    public sealed record CommandSuggestion(string Name, string Help);
    public ObservableCollection<CommandSuggestion> CommandSuggestions { get; } = [];

    // Show/filter the slash-command menu as the user types. It opens only while the line is a bare
    // command token ('/' with no space yet); once they type a space (args) or normal text, it hides.
    private void Input_TextChanged(object sender, TextChangedEventArgs e)
    {
        var t = Input.Text;
        if (t.StartsWith('/') && !t.Contains(' '))
        {
            CommandSuggestions.Clear();
            foreach (var (name, args, help) in _commands)
                if (name.StartsWith(t, StringComparison.OrdinalIgnoreCase))
                    CommandSuggestions.Add(new CommandSuggestion(
                        string.IsNullOrEmpty(args) ? name : $"{name} {args}", help));

            if (CommandSuggestions.Count > 0)
            {
                CommandPopup.IsOpen = true;
                CommandList.SelectedIndex = 0;
            }
            else CommandPopup.IsOpen = false;
        }
        else CommandPopup.IsOpen = false;
    }

    private void Input_KeyDown(object sender, KeyEventArgs e)
    {
        // While the command menu is open, the arrow/Enter/Tab/Escape keys drive it instead of the textbox.
        if (CommandPopup.IsOpen && CommandSuggestions.Count > 0)
        {
            switch (e.Key)
            {
                case Key.Down: CommandList.SelectedIndex = Math.Min(CommandList.SelectedIndex + 1, CommandSuggestions.Count - 1); e.Handled = true; return;
                case Key.Up:   CommandList.SelectedIndex = Math.Max(CommandList.SelectedIndex - 1, 0); e.Handled = true; return;
                case Key.Escape: CommandPopup.IsOpen = false; e.Handled = true; return;
                case Key.Enter:
                case Key.Tab:  AcceptSelectedCommand(); e.Handled = true; return;
            }
        }

        // Enter sends; Shift+Enter inserts a newline.
        if (e.Key == Key.Enter && (Keyboard.Modifiers & ModifierKeys.Shift) == 0)
        {
            e.Handled = true;
            Send_Click(sender, e);
        }
    }

    private void CommandList_Click(object sender, MouseButtonEventArgs e) => AcceptSelectedCommand();

    // Fill the input with the chosen command name (+ trailing space for args) and close the menu.
    private void AcceptSelectedCommand()
    {
        if (CommandList.SelectedItem is not CommandSuggestion sel) return;
        var name = sel.Name.Split(' ', 2)[0];   // drop the "[id]" args hint
        Input.Text = name + " ";
        Input.CaretIndex = Input.Text.Length;
        CommandPopup.IsOpen = false;
        Input.Focus();
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

    // Slash commands handled by the app (the Claude Code CLI runs in stateless print mode, so
    // built-in session commands like /compact aren't available there — we implement them here).
    private static readonly (string Name, string Args, string Help)[] _commands =
    [
        ("/help",    "",        "List these commands."),
        ("/compact", "",        "Summarize the conversation and replace it, to cut context size and cost."),
        ("/clear",   "",        "Start a fresh conversation (alias: /new, /reset)."),
        ("/cost",    "",        "Show this conversation's size and a rough token estimate (alias: /usage)."),
        ("/model",   "[id]",    "Show the current model, or switch it (e.g. /model claude-opus-4-8)."),
        ("/retry",   "",        "Resend your last message."),
        ("/stop",    "",        "Stop the current response."),
    ];

    private async void SendPrompt(string text)
    {
        if (text.StartsWith('/'))
        {
            await HandleSlashCommand(text);
            return;
        }

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

        // Idle timeout: cancel only after a stretch of NO activity (no streamed text, no tool
        // calls). Long agentic runs -- e.g. scanning a big modlist through Claude Code + MCP --
        // can take far longer than any fixed cap, so a flat timeout wrongly killed them. As long
        // as output or tool activity keeps arriving, the run continues; the Stop button and this
        // idle window still guarantee a truly stuck model never freezes the chat forever.
        // Claude Code's text mode can run silently for several minutes on a big task (it streams
        // only the final answer), so keep the idle window generous. The Stop button and the live
        // activity strip (spinner + elapsed timer) cover a genuinely stuck run.
        var idle = TimeSpan.FromMinutes(15);
        var cts = new System.Threading.CancellationTokenSource(idle);
        _cts = cts;

        bool started = false;
        void Append(string chunk)
        {
            cts.CancelAfter(idle);   // reset the idle window on activity
            if (!started) { started = true; reply.Text = ""; aiMsg.Text = ""; }
            // Reflect what the streamed chunk represents in the activity strip.
            const string wrench = "🔧", thought = "💭", arrow = "↳";
            if (chunk.Contains(wrench))
            {
                var s = chunk.Trim();
                var i = s.IndexOf(wrench, StringComparison.Ordinal);
                SetStatusBase($"{providerLabel} is working — {s[(i + wrench.Length)..].Trim()}");
            }
            else if (chunk.Contains(thought)) SetStatusBase($"{providerLabel} is thinking…");
            else if (!chunk.Contains(arrow)) SetStatusBase($"{providerLabel} is writing…");
            reply.Text += chunk;
            aiMsg.Text += chunk;
            Scroll.ScrollToEnd();
        }
        void Status(string s) { cts.CancelAfter(idle); reply.Text = $"_{s}_"; SetStatusBase(s); }

        SetSending(true);
        SetStatusBase($"{providerLabel} is thinking…");

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
                    + "(list_plugins, list_record_types, list_records, search_records, get_record, "
                    + "get_conflicts, get_winning_record). Use them to ground every answer in the actual "
                    + "record data. Call list_plugins first if you don't yet know what is loaded.\n\n"
                    + "## Conflicts (read this before diagnosing any override)\n"
                    + "Many plugins override the same record; only the one loaded LAST wins and is what "
                    + "the game actually uses. NEVER conclude a plugin causes a behaviour from a single "
                    + "get_record or the first override you find. To diagnose an effect (e.g. a scrap "
                    + "recipe, a leveled list, a COBJ), call get_conflicts on the record to see the full "
                    + "override chain and the WINNER, then get_winning_record for the effective values. "
                    + "Attribute the behaviour to the winning plugin, not an earlier overridden one.\n\n"
                    + "## Runtime patches (RobCo Patcher)\n"
                    + "If no plugin record explains a behaviour, the cause is usually a RobCo Patcher "
                    + "config (F4SE INI files that patch records at runtime, invisible to the plugin "
                    + "tools). Call search_robco_configs with the FormID / EditorID / component name. "
                    + "Frameworks like FallenWorld are largely config-driven this way.\n\n"
                    + "## Authoring fixes (do it yourself — never tell the user to open FO4Edit)\n"
                    + "You can finish a plugin end to end. To fix a broken record from another mod: "
                    + "copy_as_override(source_plugin, id, patch_plugin) forwards it into a patch, then edit "
                    + "the override with set_field (scalars/FormLinks), set_components (a COBJ's materials — "
                    + "JSON array of {component,count}), set_conditions (a COBJ's availability — JSON array of "
                    + "{function,param1,operator,value,runOn,reference}), add_list_item/remove_list_item "
                    + "(FormList entries), and delete_record (remove a stub). Finish with save_plugin and tell "
                    + "the user to load the patch after the conflicting plugins.";
                await _shell.Agent.RunAsync(text, sys,
                    onText: chunk => Dispatcher.BeginInvoke(() => Append(chunk)),
                    onToolStatus: status => Dispatcher.BeginInvoke(() =>
                    {
                        SetStatusBase($"{providerLabel} is working — {status}");
                        if (!started) Status($"Using {status}…");
                        else Append($"\n🔧 {status}\n");
                    }),
                    ct: cts.Token,
                    onUsage: usage => Dispatcher.BeginInvoke(() =>
                        _shell.Log.Log(LogCategory.AI, LogLevel.Info, usage)));
            }
            else
            {
                await _shell.Chat.SendAsync(text, ctx,
                    token => Dispatcher.BeginInvoke(() => Append(token)), cts.Token);
            }

            if (!started) Dispatcher.Invoke(() => reply.Text = "_(no response)_");
            _historyService.Save(_currentSession);
            _shell.Log.Log(LogCategory.AI, LogLevel.Info, "Reply received", reply.Text);

            await MaybeAutoCompact();   // keep the re-sent history from growing without bound
        }
        catch (OperationCanceledException)
        {
            var note = started ? "\n\n_[stopped]_" : "_[stopped — the AI took too long or was cancelled]_";
            reply.Text += note;
            aiMsg.Text = reply.Text;
            _historyService.Save(_currentSession);
            _shell.Log.Log(LogCategory.AI, LogLevel.Warning, "AI call cancelled/timed out", text);
        }
        catch (Exception ex)
        {
            reply.Text = $"[error] {ex.Message}";
            aiMsg.Text = reply.Text;
            _historyService.Save(_currentSession);
            _shell.Log.Log(LogCategory.AI, LogLevel.Error, "AI call failed", ex.Message);
        }
        finally
        {
            SetSending(false);
            if (ReferenceEquals(_cts, cts)) _cts = null;
            cts.Dispose();
        }
    }

    private const string CompactInstruction =
        "Summarize our conversation so far into a compact brief. Preserve every decision made, all " +
        "plugin and record names, EditorIDs and FormKeys, files created or edited, and any open or " +
        "pending tasks. Drop pleasantries and redundant detail. Output only the summary.";

    private async System.Threading.Tasks.Task HandleSlashCommand(string text)
    {
        var parts = text.Split(' ', 2, StringSplitOptions.RemoveEmptyEntries);
        var cmd = parts[0].ToLowerInvariant();
        var arg = parts.Length > 1 ? parts[1].Trim() : "";
        switch (cmd)
        {
            case "/help": case "/?": case "/commands": ShowCommandsHelp(); break;
            case "/compact":                           await CompactConversation(); break;
            case "/clear": case "/new": case "/reset":  ClearConversation(); break;
            case "/cost": case "/usage": case "/tokens": ShowUsage(); break;
            case "/model":                              SetOrShowModel(arg); break;
            case "/retry":                              RetryLast(); break;
            case "/stop":                               _cts?.Cancel(); AddInfoBubble("_Stopped._"); break;
            default:
                AddInfoBubble($"Unknown command `{cmd}`. Type `/help` for the list.");
                break;
        }
    }

    // A non-user note shown in the transcript (UI only — not sent to the AI or persisted).
    private void AddInfoBubble(string markdown)
    {
        Bubbles.Add(new ChatBubble { IsUser = false, Text = markdown });
        Scroll.ScrollToEnd();
    }

    private void ShowCommandsHelp()
    {
        var sb = new System.Text.StringBuilder("**Slash commands**\n\n");
        foreach (var (name, args, help) in _commands)
            sb.Append("- `").Append(name).Append(string.IsNullOrEmpty(args) ? "" : " " + args)
              .Append("` — ").Append(help).Append('\n');
        AddInfoBubble(sb.ToString());
    }

    // How many of the most recent messages /compact keeps verbatim (the rest are summarized).
    private const int CompactKeepRecent = 6;
    // The whole transcript is re-sent on every turn, so its size is the main cost driver. When it
    // grows past this, auto-compact the older messages so usage stops climbing turn over turn.
    private const int AutoCompactBudgetChars = 80_000;

    private async System.Threading.Tasks.Task MaybeAutoCompact()
    {
        var chars = _currentSession.Messages.Sum(m => m.Text?.Length ?? 0);
        if (chars < AutoCompactBudgetChars || _currentSession.Messages.Count <= CompactKeepRecent + 1) return;
        AddInfoBubble("_Conversation got large — auto-compacting older messages to save usage…_");
        await CompactConversation();
    }

    private async System.Threading.Tasks.Task CompactConversation()
    {
        // Split into older (to summarize) and recent (kept verbatim). Only the OLD text is compacted,
        // so the newest, most relevant detail stays intact.
        var msgs = _currentSession.Messages.Where(m => !string.IsNullOrWhiteSpace(m.Text)).ToList();
        if (msgs.Count <= CompactKeepRecent + 1)
        {
            AddInfoBubble($"_Only {msgs.Count} message(s) — nothing old enough to compact yet (keeps the last {CompactKeepRecent})._");
            return;
        }

        var older = msgs.Take(msgs.Count - CompactKeepRecent).ToList();
        var recent = msgs.Skip(msgs.Count - CompactKeepRecent).ToList();
        var transcript = string.Join("\n\n", older.Select(m => (m.IsUser ? "User: " : "Assistant: ") + m.Text));

        var cts = new System.Threading.CancellationTokenSource(TimeSpan.FromMinutes(10));
        _cts = cts;
        SetSending(true);
        SetStatusBase($"Compacting {older.Count} older message(s)…");
        try
        {
            var summary = await _shell.Chat.SummarizeTextAsync(transcript, CompactInstruction, cts.Token);
            if (string.IsNullOrWhiteSpace(summary)) { AddInfoBubble("_Compact produced no summary — conversation left unchanged._"); return; }

            // Rebuild: [summary] + the recent messages kept verbatim.
            var summaryMsg = new SessionMessage
            {
                IsUser = false,
                Text = $"🗜️ **Earlier conversation compacted** ({older.Count} messages summarized; last {recent.Count} kept):\n\n" + summary
            };
            _currentSession.Messages.Clear();
            _currentSession.Messages.Add(summaryMsg);
            foreach (var m in recent) _currentSession.Messages.Add(m);

            Bubbles.Clear();
            foreach (var m in _currentSession.Messages)
                Bubbles.Add(new ChatBubble { IsUser = m.IsUser, Text = m.Text });

            // Keep both AI histories in sync with the compacted transcript.
            var rebuilt = _currentSession.Messages
                .Select(m => new FO4RecordEditor.Models.ChatMessage(
                    m.IsUser ? FO4RecordEditor.Models.ChatRole.User : FO4RecordEditor.Models.ChatRole.Assistant, m.Text))
                .ToList();
            _shell.Chat.LoadHistory(rebuilt);
            _shell.Agent?.LoadHistory(_currentSession.Messages.Select(m => (m.IsUser, m.Text)).ToList());

            _historyService.Save(_currentSession);
            Scroll.ScrollToEnd();
        }
        catch (OperationCanceledException) { AddInfoBubble("_Compact cancelled._"); }
        catch (Exception ex) { AddInfoBubble($"[compact error] {ex.Message}"); }
        finally
        {
            SetSending(false);
            if (ReferenceEquals(_cts, cts)) _cts = null;
            cts.Dispose();
        }
    }

    private void ClearConversation()
    {
        _shell.Agent?.Reset();
        _shell.Chat.Reset();
        NewChat_Click(this, new System.Windows.RoutedEventArgs());
        AddInfoBubble("_Started a fresh conversation._");
    }

    private void ShowUsage()
    {
        int turns = _currentSession.Messages.Count;
        int chars = _currentSession.Messages.Sum(m => m.Text?.Length ?? 0);
        AddInfoBubble(
            $"**Conversation size:** {turns} message(s), ~{chars:N0} characters (≈{chars / 4:N0} tokens re-sent each turn).\n\n" +
            "This whole history is sent on every message, so cost grows as it gets longer. Run `/compact` to shrink it.");
    }

    private void SetOrShowModel(string arg)
    {
        if (string.IsNullOrWhiteSpace(arg))
        {
            AddInfoBubble($"Current model: `{_shell.Settings.Current.Model}`. Switch with `/model <id>` (e.g. `/model claude-opus-4-8`).");
            return;
        }
        _shell.Settings.Current.Model = arg;
        _shell.Settings.Save();
        _shell.RebuildProvider();
        AddInfoBubble($"Model set to `{arg}`.");
    }

    private void RetryLast()
    {
        var lastUser = _currentSession.Messages.LastOrDefault(m => m.IsUser);
        if (lastUser == null || string.IsNullOrWhiteSpace(lastUser.Text)) { AddInfoBubble("_Nothing to retry._"); return; }
        SendPrompt(lastUser.Text);
    }

    private System.Threading.CancellationTokenSource? _cts;

    private void SetSending(bool sending)
    {
        SendBtn.Visibility = sending ? System.Windows.Visibility.Collapsed : System.Windows.Visibility.Visible;
        StopBtn.Visibility = sending ? System.Windows.Visibility.Visible : System.Windows.Visibility.Collapsed;

        StatusStrip.Visibility = sending ? System.Windows.Visibility.Visible : System.Windows.Visibility.Collapsed;
        if (sending)
        {
            _sendStart = DateTime.Now;
            ElapsedText.Text = "0s";
            _statusTimer.Start();
        }
        else
        {
            _statusTimer.Stop();
        }
    }

    /// <summary>Set the activity-strip headline (e.g. "Claude is thinking…", "… is working — get_record").</summary>
    private void SetStatusBase(string text) => StatusText.Text = text;

    private void Stop_Click(object sender, System.Windows.RoutedEventArgs e) => _cts?.Cancel();

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
