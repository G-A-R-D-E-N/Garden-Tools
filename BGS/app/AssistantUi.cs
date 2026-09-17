using System;
using System.Collections.Generic;
using System.Globalization;
using System.Threading;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;

namespace BehaviourStudio.App;

internal sealed class AssistantPane : Border
{
    private readonly TextBlock _provider = new() { TextWrapping = TextWrapping.Wrap };
    private readonly TextBlock _account = new() { TextWrapping = TextWrapping.Wrap };
    private readonly TextBlock _deviceInfo = new() { TextWrapping = TextWrapping.Wrap, IsVisible = false };
    private readonly StackPanel _messages = new() { Spacing = 6 };
    private readonly TextBox _composer = new()
    {
        AcceptsReturn = true,
        TextWrapping = TextWrapping.Wrap,
        MinHeight = 70,
        MaxHeight = 150,
    };
    private readonly Button _signIn = Ux.Primary("Sign in with ChatGPT");
    private readonly Button _deviceCode = Ux.Secondary("Use device code");
    private readonly Button _signOut = Ux.Secondary("Sign out");
    private readonly Button _openUrl = Ux.Secondary("Open sign-in page");
    private readonly Button _send = Ux.Primary("Send");
    private readonly Button _cancel = Ux.Secondary("Cancel");
    private readonly Border _approval = new();
    private readonly TextBlock _status = new() { TextWrapping = TextWrapping.Wrap };
    private readonly Button _approve = Ux.Primary("Apply approved edit");
    private readonly Button _reject = Ux.Secondary("Reject");
    private string _pendingUrl = "";
    private int _messageCount;

    public AssistantPane()
    {
        Background = Ux.RailBrush;
        BorderBrush = Ux.BorderBrush;
        BorderThickness = new Thickness(1, 0, 0, 0);
        Padding = new Thickness(12);

        var title = new TextBlock
        {
            Text = "ChatGPT Assistant",
            FontSize = Ux.FontTitle,
            FontWeight = FontWeight.SemiBold,
            Foreground = Ux.TitleBrush,
        };
        var newChat = Ux.Secondary("New chat");
        var close = Ux.Secondary("Close");
        newChat.Click += (_, _) => NewChatRequested?.Invoke();
        close.Click += (_, _) => CloseRequested?.Invoke();
        var header = new DockPanel { LastChildFill = false };
        DockPanel.SetDock(close, Dock.Right);
        DockPanel.SetDock(newChat, Dock.Right);
        header.Children.Add(close);
        header.Children.Add(newChat);
        header.Children.Add(title);

        _provider.Foreground = Ux.TitleBrush;
        _provider.FontSize = Ux.FontSmall;
        _provider.Margin = new Thickness(0, 4, 0, 0);

        _account.Foreground = Ux.MetaBrush;
        _account.FontSize = Ux.FontSmall;
        _account.Margin = new Thickness(0, 2, 0, 0);

        _signIn.Click += (_, _) => SignInRequested?.Invoke();
        _deviceCode.Click += (_, _) => DeviceCodeRequested?.Invoke();
        _signOut.Click += (_, _) => SignOutRequested?.Invoke();
        _openUrl.Click += (_, _) => OpenUrlRequested?.Invoke(_pendingUrl);
        _signOut.IsVisible = false;
        _openUrl.IsVisible = false;
        var authRow = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Spacing = 6,
            Margin = new Thickness(0, 6, 0, 0),
        };
        authRow.Children.Add(_signIn);
        authRow.Children.Add(_deviceCode);
        authRow.Children.Add(_openUrl);
        authRow.Children.Add(_signOut);

        _deviceInfo.Foreground = Ux.MetaBrush;
        _deviceInfo.FontSize = Ux.FontSmall;
        _deviceInfo.Margin = new Thickness(0, 4, 0, 0);

        var notice = new TextBlock
        {
            Text = "ChatGPT sign-in happens in your browser or with a device code. BGS never asks for or stores your " +
                "ChatGPT password, OAuth token, or OpenAI API key. Opening this drawer makes no model request. " +
                "Prompts send only bounded editor context and BGS tool results when you press Send.",
            Foreground = Ux.MutedBrush,
            FontSize = Ux.FontSmall,
            TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(0, 6, 0, 8),
        };

        var conversation = new ScrollViewer
        {
            VerticalScrollBarVisibility = Avalonia.Controls.Primitives.ScrollBarVisibility.Auto,
            HorizontalScrollBarVisibility = Avalonia.Controls.Primitives.ScrollBarVisibility.Disabled,
            Content = _messages,
        };

        _approval.Background = Ux.CardBrush;
        _approval.BorderBrush = Ux.WarnBrush;
        _approval.BorderThickness = new Thickness(1);
        _approval.Padding = new Thickness(8);
        _approval.Margin = new Thickness(0, 8, 0, 0);
        _approval.IsVisible = false;
        var approvalButtons = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6 };
        _approve.Click += (_, _) => ApproveRequested?.Invoke();
        _reject.Click += (_, _) => RejectRequested?.Invoke();
        approvalButtons.Children.Add(_approve);
        approvalButtons.Children.Add(_reject);
        _approval.Child = new StackPanel
        {
            Spacing = 6,
            Children =
            {
                new TextBlock
                {
                    Text = "BGS has a pending active-editor preview. Review it before applying.",
                    Foreground = Ux.WarnBrush,
                    TextWrapping = TextWrapping.Wrap,
                },
                approvalButtons,
            },
        };

        _status.Foreground = Ux.MetaBrush;
        _status.FontSize = Ux.FontSmall;
        _status.Margin = new Thickness(0, 6, 0, 0);
        _cancel.IsVisible = false;
        _send.Click += (_, _) => SendComposer();
        _cancel.Click += (_, _) => CancelRequested?.Invoke();
        var composerButtons = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6 };
        composerButtons.Children.Add(_send);
        composerButtons.Children.Add(_cancel);

        var body = new Grid();
        body.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
        body.RowDefinitions.Add(new RowDefinition(new GridLength(1, GridUnitType.Star)));
        body.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
        var top = new StackPanel { Children = { header, _provider, _account, authRow, _deviceInfo, notice } };
        Grid.SetRow(top, 0);
        Grid.SetRow(conversation, 1);
        var bottom = new StackPanel { Children = { _approval, _composer, composerButtons, _status } };
        Grid.SetRow(bottom, 2);
        body.Children.Add(top);
        body.Children.Add(conversation);
        body.Children.Add(bottom);
        Child = body;
    }

    public event Action<string>? SendRequested;
    public event Action? CancelRequested;
    public event Action? NewChatRequested;
    public event Action? CloseRequested;
    public event Action? ApproveRequested;
    public event Action? RejectRequested;
    public event Action? SignInRequested;
    public event Action? DeviceCodeRequested;
    public event Action? SignOutRequested;
    public event Action<string>? OpenUrlRequested;

    public int MessageCount => _messageCount;
    public string ProviderStatus => _provider.Text ?? "";
    public string AccountStatus => _account.Text ?? "";
    public string DeviceInfo => _deviceInfo.Text ?? "";
    public string Status => _status.Text ?? "";
    public bool ApprovalVisible => _approval.IsVisible;
    public bool IsBusy => !_send.IsVisible;
    public bool SignInVisible => _signIn.IsVisible;
    public bool SignOutVisible => _signOut.IsVisible;
    public TextBox Composer => _composer;

    public void SetProvider(string text) => _provider.Text = text;

    public void SetAccount(string text)
    {
        _account.Text = text;
        bool signedIn = text.StartsWith("Connected", StringComparison.Ordinal);
        _signIn.IsVisible = !signedIn;
        _deviceCode.IsVisible = !signedIn;
        _signOut.IsVisible = signedIn;
    }

    public void SetDeviceCode(string url, string userCode)
    {
        _pendingUrl = url;
        bool hasCode = userCode.Length > 0;
        _openUrl.IsVisible = url.Length > 0;
        _deviceInfo.IsVisible = hasCode || url.Length > 0;
        _deviceInfo.Text = hasCode
            ? $"Enter this one-time code in the browser: {userCode}"
            : (url.Length > 0 ? "Continue in the browser to finish signing in." : "");
    }

    public void ClearDeviceCode()
    {
        _pendingUrl = "";
        _openUrl.IsVisible = false;
        _deviceInfo.IsVisible = false;
        _deviceInfo.Text = "";
    }

    public void AddUser(string text) => AddMessage("You", text, Ux.CardBrush);

    public void AddAssistant(string text)
    {
        if (text.Length > 0) AddMessage("Assistant", text, Ux.BaseBrush);
    }

    public void AddTool(string name, bool requiresApproval, string detail = "") =>
        AddMessage("BGS", $"{name}{(requiresApproval ? " (approval pending)" : "")}" +
            (detail.Length == 0 ? "" : $": {detail}"),
            Ux.RailBrush, Ux.MetaBrush, Ux.FontSmall);

    public void SetStatus(string text, IBrush brush)
    {
        _status.Text = text;
        _status.Foreground = brush;
    }

    public void SetBusy(bool busy)
    {
        _composer.IsEnabled = !busy;
        _send.IsVisible = !busy;
        _cancel.IsVisible = busy;
    }

    public void SetAuthBusy(bool busy)
    {
        _signIn.IsEnabled = !busy;
        _deviceCode.IsEnabled = !busy;
        _signOut.IsEnabled = !busy;
    }

    public void SetApproval(bool visible) => _approval.IsVisible = visible;

    public void Clear()
    {
        _messages.Children.Clear();
        _messageCount = 0;
        SetApproval(false);
        ClearDeviceCode();
        SetStatus("New chat.", Ux.MetaBrush);
    }

    private void SendComposer()
    {
        string text = (_composer.Text ?? "").Trim();
        if (text.Length == 0) return;
        _composer.Text = "";
        SendRequested?.Invoke(text);
    }

    private void AddMessage(string speaker, string text, IBrush background,
                            IBrush? foreground = null, double? fontSize = null)
    {
        var label = new TextBlock
        {
            Text = speaker,
            Foreground = Ux.TitleBrush,
            FontSize = Ux.FontSmall,
            FontWeight = FontWeight.SemiBold,
        };
        var content = new TextBlock
        {
            Text = text,
            Foreground = foreground ?? Ux.MetaBrush,
            FontSize = fontSize ?? Ux.FontBody,
            TextWrapping = TextWrapping.Wrap,
        };
        _messages.Children.Add(new Border
        {
            Background = background,
            BorderBrush = Ux.BorderBrush,
            BorderThickness = new Thickness(1),
            Padding = new Thickness(8),
            Child = new StackPanel { Spacing = 3, Children = { label, content } },
        });
        _messageCount++;
    }
}

internal sealed class AssistantUi
{
    private const double DefaultWidth = 420;
    private const double MinimumWidth = 320;
    private const double MaximumWidth = 520;
    private const string WidthSetting = "assistant.drawer_width";

    private readonly MainWindow _owner;
    private readonly AssistantPane _pane = new();
    private readonly ColumnDefinition _splitterColumn = new(new GridLength(0, GridUnitType.Pixel));
    private readonly ColumnDefinition _drawerColumn =
        new(new GridLength(0, GridUnitType.Pixel)) { MinWidth = 0, MaxWidth = MaximumWidth };
    private readonly GridSplitter _splitter;
    private AssistantTools? _tools;
    private IAssistantSession? _session;
    private CodexAssistantConnection? _connection;
    private CancellationTokenSource? _requestCancellation;
    private bool _open;

    public AssistantUi(MainWindow owner, Grid root, EditorShell shell)
    {
        _owner = owner ?? throw new ArgumentNullException(nameof(owner));
        root.ColumnDefinitions.Add(_splitterColumn);
        root.ColumnDefinitions.Add(_drawerColumn);
        _splitter = new GridSplitter
        {
            Width = 6,
            Background = Brushes.Transparent,
            IsVisible = false,
        };
        Grid.SetColumn(_splitter, 1);
        Grid.SetColumn(_pane, 2);
        root.Children.Add(_splitter);
        root.Children.Add(_pane);

        var chat = Ux.Primary("Chat");
        ToolTip.SetTip(chat, "Open ChatGPT Assistant.");
        chat.Click += (_, _) =>
        {
            SetOpen(true);
            _pane.SetStatus("Ready. Type a message below to chat with ChatGPT through Codex.", Ux.MetaBrush);
            _pane.Composer.Focus();
        };
        var settings = Ux.Secondary("Assistant settings");
        ToolTip.SetTip(settings, "Open ChatGPT account and Codex connection settings.");
        settings.Click += (_, _) =>
        {
            SetOpen(true);
            _pane.SetStatus(
                "Assistant settings. Sign in with ChatGPT above, use a device code, or sign out. Credentials are never entered into BGS.",
                Ux.MetaBrush);
        };
        shell.Tools.Children.Add(Ux.Group("ChatGPT", Ux.Wrap(chat, settings)));
        _pane.NewChatRequested += NewChat;
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
        _pane.SetStatus("Ready. Click Chat to start or Assistant settings to sign in.", Ux.MetaBrush);
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

    internal void SetSessionForTest(IAssistantSession session, AssistantTools? tools = null)
    {
        if (tools != null) _tools = tools;
        _tools ??= _owner.CreateAssistantTools();
        _session?.Dispose();
        _session = session;
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

    private async void Send(string prompt)
    {
        if (_pane.IsBusy) return;
        _pane.AddUser(prompt);
        _pane.SetBusy(true);
        _requestCancellation?.Dispose();
        _requestCancellation = new CancellationTokenSource();
        try
        {
            if (!EnsureSession()) return;
            var reply = await _session!.SendAsync(prompt, _owner.AssistantContextSnapshot,
                _requestCancellation.Token).ConfigureAwait(true);
            foreach (var tool in reply.Tools) _pane.AddTool(tool.Name, tool.RequiresApproval);
            _pane.AddAssistant(reply.Text);
            _pane.SetApproval(reply.AwaitingApproval);
            _pane.SetStatus(ReplyStatus(reply),
                reply.Status == "ok" ? new SolidColorBrush(Ux.Good) : Ux.WarnBrush);
            if (reply.Status == "signed_out") RefreshAccountText();
        }
        finally
        {
            _pane.SetBusy(false);
            _requestCancellation?.Dispose();
            _requestCancellation = null;
        }
    }

    private static string ReplyStatus(AssistantReply reply) =>
        reply.Status == "ok" ? "Ready." : reply.Text;

    private bool EnsureSession()
    {
        if (_session != null) return true;
        if (!EnsureCodex()) return false;
        _tools = _owner.CreateAssistantTools();
        _session = _owner.CreateCodexSession(_connection!, _tools);
        return true;
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
            _session?.ClearHistory();
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

    private void Cancel() => _requestCancellation?.Cancel();

    private void NewChat()
    {
        _session?.ClearHistory();
        _tools?.RejectPendingClipAnimation();
        _pane.Clear();
    }

    private void Approve()
    {
        if (_tools == null) return;
        var result = _tools.ApprovePendingClipAnimation();
        _pane.SetApproval(false);
        _pane.AddTool("bgs.set_clip_animation", false, result.Applied ? "applied" : result.Message);
        _pane.SetStatus(result.Applied ? "Applied in the editor; save remains explicit." : result.Message,
            result.Applied ? new SolidColorBrush(Ux.Good) : Ux.WarnBrush);
    }

    private void Reject()
    {
        _tools?.RejectPendingClipAnimation();
        _pane.SetApproval(false);
        _pane.SetStatus("The proposed edit was rejected.", Ux.MetaBrush);
    }

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