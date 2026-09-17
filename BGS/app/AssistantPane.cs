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
    private readonly StackPanel _messages = new() { Spacing = 6 };
    private readonly TextBox _composer = new()
    {
        AcceptsReturn = true,
        TextWrapping = TextWrapping.Wrap,
        MinHeight = 54,
        MaxHeight = 140,
    };
    private readonly ComboBox _modelSelect = new() { PlaceholderText = "Model", MinWidth = 150 };
    private readonly Button _modelReset = Ux.Secondary("\u21BA");
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
        TextTrimming = TextTrimming.CharacterEllipsis,
        VerticalAlignment = VerticalAlignment.Center,
    };
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
        var header = new DockPanel { LastChildFill = true };
        DockPanel.SetDock(newChat, Dock.Right);
        DockPanel.SetDock(close, Dock.Right);
        header.Children.Add(newChat);
        header.Children.Add(close);
        header.Children.Add(_chatHeader);
        header.Margin = new Thickness(0, 0, 0, 8);

        var conversation = new ScrollViewer
        {
            VerticalScrollBarVisibility = Avalonia.Controls.Primitives.ScrollBarVisibility.Auto,
            HorizontalScrollBarVisibility = Avalonia.Controls.Primitives.ScrollBarVisibility.Disabled,
            Content = _messages,
        };

        _approval.Background = Ux.CardBrush;
        _approval.BorderBrush = Ux.WarnBrush;
        _approval.BorderThickness = new Thickness(1);
        _approval.CornerRadius = new CornerRadius(Ux.Radius);
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
        _status.TextTrimming = TextTrimming.CharacterEllipsis;
        _status.VerticalAlignment = VerticalAlignment.Center;

        _modelReset.FontSize = 12;
        _modelReset.Padding = new Thickness(8, 2, 8, 2);
        _modelReset.Margin = new Thickness(4, 0, 0, 0);
        _modelReset.IsVisible = false;
        ToolTip.SetTip(_modelReset, "Use the provider's own default model");

        _send.Click += (_, _) => SendComposer();
        _cancel.Click += (_, _) => CancelRequested?.Invoke();
        _cancel.IsVisible = false;

        var toolbar = new DockPanel { LastChildFill = true, Margin = new Thickness(0, 6, 0, 0) };
        DockPanel.SetDock(_send, Dock.Right);
        DockPanel.SetDock(_cancel, Dock.Right);
        DockPanel.SetDock(_modelReset, Dock.Right);
        DockPanel.SetDock(_modelSelect, Dock.Right);
        toolbar.Children.Add(_send);
        toolbar.Children.Add(_cancel);
        toolbar.Children.Add(_modelReset);
        toolbar.Children.Add(_modelSelect);
        toolbar.Children.Add(_status);

        var bottom = new StackPanel
        {
            Spacing = 0,
            Children = { _approval, _composer, toolbar },
        };

        var body = new Grid();
        body.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
        body.RowDefinitions.Add(new RowDefinition(new GridLength(1, GridUnitType.Star)));
        body.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
        Grid.SetRow(header, 0);
        Grid.SetRow(conversation, 1);
        Grid.SetRow(bottom, 2);
        body.Children.Add(header);
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
    public event Action<string>? ModelSelected;
    public event Action? ModelResetRequested;

    public int MessageCount => _messageCount;
    public bool IsBusy => !_send.IsVisible;
    public bool ModelSelectorVisible => _modelSelect.IsVisible;
    public bool ModelResetVisible
    {
        get => _modelReset.IsVisible;
        internal set => _modelReset.IsVisible = value;
    }
    public string? SelectedModelItem => (_modelSelect.SelectedItem as AssistantModelItem)?.WireModel;
    internal ComboBox ModelSelector => _modelSelect;
    internal Button ModelReset => _modelReset;
    internal void InvokeModelSelected(string wireModel) => ModelSelected?.Invoke(wireModel);
    internal void InvokeModelResetRequested() => ModelResetRequested?.Invoke();
    public TextBox Composer => _composer;
    public string ChatTitle
    {
        get => _chatHeader.Text ?? "";
        set => _chatHeader.Text = value;
    }
    public string Status => _status.Text ?? "";
    public bool ApprovalVisible => _approval.IsVisible;
    public void SetDeviceCode(string url, string userCode)
    {
    }
    public void ClearDeviceCode()
    {
    }
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
    }
    public void SetApproval(bool visible) => _approval.IsVisible = visible;
    public void Clear()
    {
        _messages.Children.Clear();
        _messageCount = 0;
        SetApproval(false);
        SetStatus("New chat.", Ux.MetaBrush);
    }

    public void AddUser(string text) => AddMessage("You", text, Ux.CardBrush);
    public void AddAssistant(string text)
    {
        if (text.Length > 0) AddMessage("Assistant", text, Ux.BaseBrush, markdown: true);
    }
    public void AddTool(string name, bool requiresApproval, string detail = "") =>
        AddMessage("BGS", $"{name}{(requiresApproval ? " (approval pending)" : "")}" +
            (detail.Length == 0 ? "" : $": {detail}"),
            Ux.RailBrush, Ux.MetaBrush, Ux.FontSmall);

    private void SendComposer()
    {
        string text = (_composer.Text ?? "").Trim();
        if (text.Length == 0) return;
        _composer.Text = "";
        SendRequested?.Invoke(text);
    }

    private void AddMessage(string speaker, string text, IBrush background,
                            IBrush? foreground = null, double? fontSize = null, bool markdown = false)
    {
        var label = new TextBlock
        {
            Text = speaker,
            Foreground = Ux.TitleBrush,
            FontSize = Ux.FontSmall,
            FontWeight = FontWeight.SemiBold,
        };
        Control content = markdown
            ? AssistantMarkdown.Render(text)
            : new TextBlock
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
            CornerRadius = new CornerRadius(Ux.Radius),
            Padding = new Thickness(10),
            Child = new StackPanel { Spacing = 3, Children = { label, content } },
        });
        _messageCount++;
    }
}
