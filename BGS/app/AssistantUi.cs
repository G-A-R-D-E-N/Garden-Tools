using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Threading;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Threading;

namespace BehaviourStudio.App;

internal sealed class AssistantUi
{
    private const double DefaultWidth = 760;
    private const double MinimumWidth = 520;
    private const double MaximumWidth = 1120;
    private const double SidebarWidth = 220;
    private const string WidthSetting = "assistant.drawer_width";
    private const string ActiveChatSetting = "assistant.active_chat_id";

    private readonly MainWindow _owner;
    private readonly AssistantPane _pane = new();
    private readonly AssistantConversationSidebar _sidebar = new();
    private readonly ColumnDefinition _splitterColumn = new(new GridLength(0, GridUnitType.Pixel));
    private readonly ColumnDefinition _sidebarColumn =
        new(new GridLength(SidebarWidth, GridUnitType.Pixel)) { MinWidth = 160, MaxWidth = 280 };
    private readonly ColumnDefinition _drawerColumn =
        new(new GridLength(0, GridUnitType.Pixel)) { MinWidth = 0, MaxWidth = MaximumWidth };
    private readonly GridSplitter _splitter;
    private CodexAssistantConnection? _connection;
    private AssistantTools? _tools;
    private AssistantConversationController? _controller;
    private CancellationTokenSource? _requestCancellation;
    private bool _open;

    public AssistantUi(MainWindow owner, Grid root, EditorShell shell)
    {
        _owner = owner ?? throw new ArgumentNullException(nameof(owner));
        root.ColumnDefinitions.Add(_sidebarColumn);
        root.ColumnDefinitions.Add(_splitterColumn);
        root.ColumnDefinitions.Add(_drawerColumn);
        _splitter = new GridSplitter
        {
            Width = 6,
            Background = Avalonia.Media.Brushes.Transparent,
            IsVisible = false,
        };
        Grid.SetColumn(_sidebar, 0);
        Grid.SetColumn(_splitter, 1);
        Grid.SetColumn(_pane, 2);
        root.Children.Add(_sidebar);
        root.Children.Add(_splitter);
        root.Children.Add(_pane);

        var chat = Ux.Primary("Chat");
        ToolTip.SetTip(chat, "Open your saved chat list and current conversation.");
        chat.Click += (_, _) => OpenAsChat();
        var settings = Ux.Secondary("Assistant settings");
        ToolTip.SetTip(settings, "Open ChatGPT account and Codex connection settings.");
        settings.Click += (_, _) => OpenAsSettings();
        shell.Tools.Children.Add(Ux.Group("ChatGPT", Ux.Wrap(chat, settings)));

        RestoreChats();

        _sidebar.NewChatRequested += NewChatInternal;
        _sidebar.ConversationSelected += Select;
        _sidebar.RenameCommitted += RenameCommitted;
        _sidebar.DeleteRequested += Delete;

        _pane.NewChatRequested += NewChatInternal;
        _pane.CloseRequested += () => SetOpen(false);
        _pane.CancelRequested += Cancel;
        _pane.SendRequested += Send;
        _pane.ApproveRequested += Approve;
        _pane.RejectRequested += Reject;
        _pane.SignInRequested += SignIn;
        _pane.DeviceCodeRequested += DeviceCode;
        _pane.SignOutRequested += SignOut;
        _pane.OpenUrlRequested += OpenUrl;

        AssistantProviderOptions options = AssistantProviderOptions.FromSettings();
        bool detected = AssistantProvider.TryResolveExecutable(options, out CodexExecutable? executable, out _);
        _pane.SetProvider(AssistantProvider.Describe(options, detected ? executable : null));
        _pane.SetAccount("Signed out");
        _pane.SetStatus("Pick a chat or click + New chat to start.", Ux.MetaBrush);
        SetOpen(false);
    }

    public bool IsOpen => _open;
    public bool IsBusy => _pane.IsBusy;
    public int MessageCount => _pane.MessageCount;
    public string ProviderStatus => _pane.ProviderStatus;
    public string AccountStatus => _pane.AccountStatus;
    public string DeviceInfo => _pane.DeviceInfo;
    public string Status => _pane.Status;
    public bool ApprovalVisible => _pane.ApprovalVisible;
    public bool SignInVisible => _pane.SignInVisible;
    public AssistantPane Pane => _pane;

    internal AssistantUi AssistantUiForTest => this;

    internal void SetSessionForTest(IAssistantSession session, AssistantTools? tools = null)
    {
        if (tools != null) _tools = tools;
        _tools ??= _owner.CreateAssistantTools();
        AssistantConversationEntry? entry = _controller?.Active;
        if (entry is null) return;
        entry.Session?.Dispose();
        entry.Session = session;
        entry.HasPendingApproval = () => _tools!.HasPendingApproval;
    }

    internal IReadOnlyList<AssistantChatSummary> ChatListForTest =>
        _controller?.List.Summaries() ?? Array.Empty<AssistantChatSummary>();

    internal string? ActiveChatIdForTest => _controller?.List.ActiveChatId;

    internal void SelectChatForTest(string chatId)
    {
        if (_controller is null) return;
        if (_controller.List.IsBusy) return;
        if (_controller.TrySelectById(chatId))
        {
            AssistantConversationEntry? previous = _controller.List.Entries
                .FirstOrDefault(e => !ReferenceEquals(e, _controller.Active));
            previous?.ClearPendingApproval();
            OnSelectionChanged(_controller.List.ActiveIndex);
        }
    }

    private void RestoreChats()
    {
        IReadOnlyList<AssistantChatSummary> summaries = AssistantChatStore.All();
        var loaded = new List<AssistantChat>(summaries.Count);
        foreach (AssistantChatSummary summary in summaries)
        {
            AssistantChat? chat = AssistantChatStore.Load(summary.Id);
            if (chat != null) loaded.Add(chat);
        }
        loaded = loaded.OrderByDescending(c => c.UpdatedUtc).Take(AssistantChatStore.MaxConversations).ToList();

        string? activeId = Settings.Get(ActiveChatSetting);

        var sessionFactory = new Func<string, IAssistantSession?>(chatId => CreateSessionForChat(chatId));
        _controller = new AssistantConversationController(sessionFactory, loaded, activeId);
        _controller.List.StructureChanged += RefreshSidebar;
        _controller.List.SelectionChanged += OnSelectionChanged;
        RefreshSidebar();
        OnSelectionChanged(_controller.List.ActiveIndex);
    }

    private IAssistantSession? CreateSessionForChat(string chatId)
    {
        if (!EnsureCodex()) return null;
        AssistantTools tools = _tools ?? (_tools = _owner.CreateAssistantTools());
        return new CodexAssistantSession(_connection!, tools.Functions, () => tools!.HasPendingApproval);
    }

    private void RefreshSidebar()
    {
        if (_controller is null) return;
        IReadOnlyList<AssistantChatSummary> summaries = _controller.List.Summaries();
        _sidebar.Bind(summaries, _controller.List.ActiveChatId);
    }

    private void OnSelectionChanged(int index)
    {
        if (_controller is null) return;
        AssistantConversationEntry? entry = _controller.Active;
        _pane.Clear();
        if (entry is not null)
        {
            _pane.ChatTitle = entry.Chat.Title;
            foreach (AssistantChatMessage message in entry.Chat.Messages)
            {
                switch (message.Role)
                {
                    case AssistantChatRole.User:
                        _pane.AddUser(message.Text);
                        break;
                    case AssistantChatRole.Assistant:
                        _pane.AddAssistant(message.Text);
                        break;
                    case AssistantChatRole.Tool:
                        _pane.AddTool(
                            message.ToolName ?? "tool",
                            message.ToolRequiresApproval ?? false);
                        break;
                }
            }
            _pane.SetApproval(entry.PendingApproval);
        }
        else
        {
            _pane.ChatTitle = "(no chat selected)";
        }
        RefreshSidebar();
        SaveActiveChatId();
    }

    private void SaveActiveChatId()
    {
        if (_controller is null) return;
        Settings.TrySet(ActiveChatSetting, _controller.List.ActiveChatId ?? "", out _);
    }

    private void SetOpen(bool open)
    {
        if (open)
        {
            double stored = ReadStoredWidth();
            _drawerColumn.Width = new GridLength(stored, GridUnitType.Pixel);
        }
        else
        {
            if (_open) StoreWidth(_drawerColumn.Width.Value);
            _drawerColumn.Width = new GridLength(0, GridUnitType.Pixel);
        }
        _open = open;
        _splitterColumn.Width = new GridLength(open ? 6 : 0, GridUnitType.Pixel);
        _pane.IsVisible = open;
        _splitter.IsVisible = open;
    }

    private void OpenAsChat()
    {
        SetOpen(true);
        _pane.SetStatus("Ready. Type below to send to the selected chat.", Ux.MetaBrush);
        Dispatcher.UIThread.Post(() => _pane.Composer.Focus());
    }

    private void OpenAsSettings()
    {
        SetOpen(true);
        _pane.SetStatus(
            "Assistant settings. Sign in with ChatGPT above, use a device code, or sign out. Credentials are never entered into BGS.",
            Ux.MetaBrush);
    }

    private void NewChatInternal()
    {
        if (_controller is null) return;
        if (_controller.List.IsBusy)
        {
            _pane.SetStatus("Wait for the current request to finish before starting a new chat.", Ux.MetaBrush);
            return;
        }
        AssistantConversationEntry? entry = _controller.NewChat();
        if (entry is null)
        {
            _pane.SetStatus("BGS kept your existing chats (max reached). Try renaming instead.", Ux.MetaBrush);
            return;
        }
        _pane.SetStatus("Started a new chat. Type below to send.", Ux.MetaBrush);
        RefreshSidebar();
        OnSelectionChanged(_controller.List.ActiveIndex);
    }

    private void Select(string chatId)
    {
        if (_controller is null || string.IsNullOrEmpty(chatId)) return;
        if (_controller.List.IsBusy)
        {
            _pane.SetStatus("Wait for the current request to finish before switching chats.", Ux.MetaBrush);
            return;
        }
        if (!_controller.TrySelectById(chatId)) return;
        AssistantConversationEntry? previous = _controller.List.Entries
            .FirstOrDefault(e => !ReferenceEquals(e, _controller.Active));
        previous?.ClearPendingApproval();
        OnSelectionChanged(_controller.List.ActiveIndex);
    }

    private void RenameCommitted(string chatId, string newTitle)
    {
        if (_controller is null) return;
        int idx = _controller.List.IndexOf(chatId);
        if (idx < 0) return;
        _controller.Rename(idx, newTitle);
        RefreshSidebar();
        if (_controller.List.ActiveChatId == chatId)
            _pane.ChatTitle = _controller.List.Active!.Chat.Title;
    }

    private void Delete(string chatId)
    {
        if (_controller is null) return;
        int idx = _controller.List.IndexOf(chatId);
        if (idx < 0) return;
        _controller.Delete(idx);
        RefreshSidebar();
        OnSelectionChanged(_controller.List.ActiveIndex);
    }

    private async void Send(string prompt)
    {
        if (_controller is null || _controller.List.IsBusy) return;
        AssistantConversationEntry? entry = _controller.Active;
        if (entry is null) return;

        _pane.AddUser(prompt);
        _pane.SetBusy(true);
        _controller.List.IsBusy = true;
        _requestCancellation?.Dispose();
        _requestCancellation = new CancellationTokenSource();
        try
        {
            AssistantReply reply = await _controller
                .SendAsync(prompt, _owner.AssistantContextSnapshot, _requestCancellation.Token)
                .ConfigureAwait(true);
            OnSelectionChanged(_controller.List.ActiveIndex);
            string statusText = reply.Status switch
            {
                "ok" => "Ready.",
                "cancelled" => "Cancelled.",
                "busy" => "BGS is processing another request.",
                "signed_out" => "Sign in with ChatGPT to use the assistant.",
                _ => "The assistant did not return a reply.",
            };
            _pane.SetStatus(statusText,
                reply.Status == "ok" ? new Avalonia.Media.SolidColorBrush(Ux.Good) : Ux.WarnBrush);
        }
        catch (OperationCanceledException)
        {
            OnSelectionChanged(_controller.List.ActiveIndex);
            _pane.SetStatus("Cancelled.", Ux.MetaBrush);
        }
        finally
        {
            _controller.List.IsBusy = false;
            _pane.SetBusy(false);
            _requestCancellation?.Dispose();
            _requestCancellation = null;
        }
    }

    private void Cancel() => _requestCancellation?.Cancel();

    private void Approve()
    {
        AssistantTools? tools = _tools ?? (_tools = _owner.CreateAssistantTools());
        tools.ApprovePendingClipAnimation();
        _pane.SetApproval(false);
        _pane.AddTool("bgs.set_clip_animation", false, "applied");
        _pane.SetStatus("Applied in the editor; save remains explicit.",
            new Avalonia.Media.SolidColorBrush(Ux.Good));
    }

    private void Reject()
    {
        AssistantTools? tools = _tools ?? (_tools = _owner.CreateAssistantTools());
        tools.RejectPendingClipAnimation();
        _pane.SetApproval(false);
        _pane.SetStatus("The proposed edit was rejected.", Ux.MetaBrush);
    }

    private bool EnsureCodex()
    {
        if (_connection != null) return true;
        AssistantProviderOptions options = AssistantProviderOptions.FromSettings();
        if (!AssistantProvider.TryResolveExecutable(options, out _, out string error))
        {
            _pane.SetStatus(error, Ux.WarnBrush);
            return false;
        }
        _connection = _owner.CreateCodexConnection(options);
        _connection.Changed += OnConnectionChanged;
        return true;
    }

    private void OnConnectionChanged() => Dispatcher.UIThread.Post(RefreshAccountText);

    private async void SignIn()
    {
        _pane.SetAuthBusy(true);
        try
        {
            if (!EnsureCodex()) return;
            CodexAccount account = await _connection!.ConnectAsync().ConfigureAwait(true);
            RefreshAccountText();
            if (account.SignedIn)
            {
                _pane.SetStatus("Connected. Opening the assistant made no model request.", Ux.MetaBrush);
                return;
            }
            CodexLoginChallenge challenge = await _connection.BeginChatGptLoginAsync().ConfigureAwait(true);
            if (challenge.HasBrowserUrl)
            {
                OpenUrl(challenge.AuthUrl);
                _pane.SetStatus("Waiting for browser sign-in to finish...", Ux.MetaBrush);
            }
            else
            {
                _pane.SetStatus("Waiting for sign-in to finish...", Ux.MetaBrush);
            }
        }
        catch (CodexProtocolException exception)
        {
            _pane.SetStatus("Disconnected: " + exception.Message, Ux.WarnBrush);
        }
        catch (Exception exception) when (exception is InvalidOperationException or TimeoutException)
        {
            _pane.SetStatus("Disconnected: Codex sign-in could not be started.", Ux.WarnBrush);
        }
        finally
        {
            _pane.SetAuthBusy(false);
        }
    }

    private async void DeviceCode()
    {
        _pane.SetAuthBusy(true);
        try
        {
            if (!EnsureCodex()) return;
            await _connection!.ConnectAsync().ConfigureAwait(true);
            CodexLoginChallenge challenge = await _connection.BeginDeviceCodeLoginAsync().ConfigureAwait(true);
            _pane.SetDeviceCode(challenge.VerificationUrl, challenge.UserCode);
            OpenUrl(challenge.VerificationUrl);
            _pane.SetStatus("Waiting for device-code sign-in to finish...", Ux.MetaBrush);
        }
        catch (CodexProtocolException exception)
        {
            _pane.SetStatus("Disconnected: " + exception.Message, Ux.WarnBrush);
        }
        catch (Exception exception) when (exception is InvalidOperationException or TimeoutException)
        {
            _pane.SetStatus("Disconnected: device-code sign-in could not be started.", Ux.WarnBrush);
        }
        finally
        {
            _pane.SetAuthBusy(false);
        }
    }

    private async void SignOut()
    {
        _pane.SetAuthBusy(true);
        try
        {
            if (_connection is null) return;
            await _connection.SignOutAsync().ConfigureAwait(true);
            foreach (AssistantConversationEntry entry in _controller!.List.Entries)
                entry.ClearPendingApproval();
            RefreshAccountText();
            _pane.SetStatus("Signed out. Codex owns the remaining credential state.", Ux.MetaBrush);
        }
        catch (CodexProtocolException exception)
        {
            _pane.SetStatus("Disconnected: " + exception.Message, Ux.WarnBrush);
        }
        finally
        {
            _pane.SetAuthBusy(false);
        }
    }

    private void RefreshAccountText()
    {
        if (_controller is null) return;
        if (_connection is null || !_connection.IsConnected)
        {
            _pane.SetAccount("Signed out");
            return;
        }
        CodexAccount account = _connection.Account;
        if (!account.SignedIn)
        {
            _pane.SetAccount("Signed out");
            return;
        }

        string plan = PlanLabel(account.PlanType);
        string text = plan.Length > 0
            ? $"Connected · ChatGPT {plan}"
            : $"Connected · {account.AuthMode}";
        if (_connection.ModelLabel.Length > 0) text += $"\nModel: {_connection.ModelLabel}";
        _pane.SetAccount(text);
    }

    internal static string PlanLabel(string planType) =>
        planType.Length == 0
            ? ""
            : char.ToUpperInvariant(planType[0]) + planType[1..].ToLowerInvariant();

    private static void OpenUrl(string url)
    {
        if (string.IsNullOrWhiteSpace(url)) return;
        try
        {
            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
            {
                FileName = url,
                UseShellExecute = true,
            });
        }
        catch (Exception exception) when (exception is InvalidOperationException or System.ComponentModel.Win32Exception)
        {
        }
    }

    private void SetApproval(bool visible) => _pane.SetApproval(visible);

    private static double ReadStoredWidth()
    {
        string stored = Settings.Get(WidthSetting);
        if (double.TryParse(stored, NumberStyles.Float, CultureInfo.InvariantCulture, out double width) &&
            width >= MinimumWidth && width <= MaximumWidth)
            return width;
        return DefaultWidth;
    }

    private static void StoreWidth(double width)
    {
        if (width < MinimumWidth || width > MaximumWidth) return;
        Settings.TrySet(WidthSetting, width.ToString("R", CultureInfo.InvariantCulture), out _);
    }
}
