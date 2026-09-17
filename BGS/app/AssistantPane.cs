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
    private readonly ComboBox _modelSelect = new() { PlaceholderText = "Model" };
    private readonly Button _modelReset = Ux.Secondary("Reset model");
    private readonly ComboBox _providerSelect = new() { PlaceholderText = "Provider" };
    private readonly TextBlock _notice = new();
    private bool _codexManaged = true;
    private readonly Button _send = Ux.Primary("Send");
    private readonly Button _cancel = Ux.Secondary("Cancel");
    private readonly Border _approval = new();
    private readonly TextBlock _status = new() { TextWrapping = TextWrapping.Wrap };
    private readonly Button _approve = Ux.Primary("Apply approved edit");
    private readonly Button _reject = Ux.Secondary("Reject");
    private readonly TextBlock _chatHeader = new()
    {
        Text = "(no chat selected)",
        Foreground = Ux.TitleBrush,
        FontSize = Ux.FontTitle,
        FontWeight = FontWeight.SemiBold,
        Margin = new Thickness(0, 0, 0, 6),
    };
    private string _pendingUrl = "";
    private int _messageCount;

    public AssistantPane()
    {
        Background = Ux.RailBrush;
        BorderBrush = Ux.BorderBrush;
        BorderThickness = new Thickness(0);
        Padding = new Thickness(12);

        var newChat = Ux.Secondary("New chat");
        var close = Ux.Secondary("Close");
        newChat.Click += (_, _) => NewChatRequested?.Invoke();
        close.Click += (_, _) => CloseRequested?.Invoke();
        var header = new DockPanel { LastChildFill = false };
        DockPanel.SetDock(close, Dock.Right);
        DockPanel.SetDock(newChat, Dock.Right);
        header.Children.Add(close);
        header.Children.Add(newChat);

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

        _modelSelect.IsVisible = false;
        _modelSelect.MinWidth = 180;
        _modelSelect.HorizontalAlignment = HorizontalAlignment.Left;
        var providerRow = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Spacing = 6,
            Margin = new Thickness(0, 6, 0, 0),
        };
        providerRow.Children.Add(new TextBlock
        {
            Text = "Provider",
            Foreground = Ux.MetaBrush,
            FontSize = Ux.FontSmall,
            VerticalAlignment = VerticalAlignment.Center,
        });
        _providerSelect.MinWidth = 180;
        _providerSelect.HorizontalAlignment = HorizontalAlignment.Left;
        providerRow.Children.Add(_providerSelect);

        var modelRow = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Spacing = 6,
            Margin = new Thickness(0, 6, 0, 0),
        };
        modelRow.Children.Add(_modelSelect);
        modelRow.Children.Add(_modelReset);
        _modelReset.IsVisible = false;

        _deviceInfo.Foreground = Ux.MetaBrush;
        _deviceInfo.FontSize = Ux.FontSmall;
        _deviceInfo.Margin = new Thickness(0, 4, 0, 0);

        _notice.Text = CodexNotice;
        _notice.Foreground = Ux.MutedBrush;
        _notice.FontSize = Ux.FontSmall;
        _notice.TextWrapping = TextWrapping.Wrap;
        _notice.Margin = new Thickness(0, 6, 0, 8);

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
        var top = new StackPanel { Children = { header, _chatHeader, _provider, _account, authRow,
            providerRow, modelRow, _deviceInfo, _notice } };
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
    public event Action<string>? ModelSelected;
    public event Action? ModelResetRequested;
    public event Action<string>? ProviderSelected;

    public int MessageCount => _messageCount;
    public string ProviderStatus => _provider.Text ?? "";
    public string AccountStatus => _account.Text ?? "";
    public string DeviceInfo => _deviceInfo.Text ?? "";
    public string Status => _status.Text ?? "";
    public bool ApprovalVisible => _approval.IsVisible;
    public bool IsBusy => !_send.IsVisible;
    public bool SignInVisible => _signIn.IsVisible;
    public bool SignOutVisible => _signOut.IsVisible;
    public bool ModelSelectorVisible => _modelSelect.IsVisible;
    public bool ModelResetVisible
    {
        get => _modelReset.IsVisible;
        internal set => _modelReset.IsVisible = value;
    }
    public string? SelectedModelItem =>
        (_modelSelect.SelectedItem as AssistantModelItem)?.WireModel;
    internal ComboBox ModelSelector => _modelSelect;
    internal Button ModelReset => _modelReset;
    internal ComboBox ProviderSelector => _providerSelect;
    internal void InvokeModelSelected(string wireModel) => ModelSelected?.Invoke(wireModel);
    internal void InvokeModelResetRequested() => ModelResetRequested?.Invoke();
    internal void InvokeProviderSelected(string backend) => ProviderSelected?.Invoke(backend);

    public bool ProviderSelectorVisible => _providerSelect.IsVisible;
    public string? SelectedProvider => (_providerSelect.SelectedItem as AssistantProviderItem)?.Wire;
    public string NoticeText => _notice.Text ?? "";

    public const string CodexNotice =
        "ChatGPT sign-in happens in your browser or with a device code. BGS never asks for or stores your " +
        "ChatGPT password, OAuth token, or OpenAI API key. Opening this drawer makes no model request. " +
        "Prompts send only bounded editor context and BGS tool results when you press Send.";

    public const string CliNotice =
        "This provider signs in through its own command-line tool, so BGS never sees or stores a credential. " +
        "Opening this drawer makes no model request. Prompts send only bounded editor context and BGS tool " +
        "results when you press Send.";

    public void SetAuthProvider(bool codexManaged)
    {
        _codexManaged = codexManaged;
        _signIn.IsVisible = codexManaged;
        _deviceCode.IsVisible = codexManaged;
        _openUrl.IsVisible = codexManaged;
        _signOut.IsVisible = codexManaged;
        _notice.Text = codexManaged ? CodexNotice : CliNotice;
    }
    public TextBox Composer => _composer;
    public string ChatTitle
    {
        get => _chatHeader.Text ?? "";
        set => _chatHeader.Text = value ?? "";
    }

    public void SetProvider(string text) => _provider.Text = text;
    public void SetAccount(string text)
    {
        _account.Text = text;
        if (!_codexManaged) return;
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
        _modelSelect.IsEnabled = !busy;
        _modelReset.IsEnabled = !busy;
        _providerSelect.IsEnabled = !busy;
    }

    public void SetAuthBusy(bool busy)
    {
        _signIn.IsEnabled = !busy;
        _deviceCode.IsEnabled = !busy;
        _signOut.IsEnabled = !busy;
        _modelSelect.IsEnabled = !busy;
        _modelReset.IsEnabled = !busy;
        _providerSelect.IsEnabled = !busy;
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
