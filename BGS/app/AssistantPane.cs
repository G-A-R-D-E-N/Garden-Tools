using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;

namespace BehaviourStudio.App;

internal sealed class AssistantPane : Border
{
    private readonly StackPanel _messages = new() { Spacing = 6 };
    private readonly ScrollViewer _conversation;
    private readonly StackPanel _queuePanel = new() { Spacing = 3 };
    private readonly Border _quickStart = new()
    {
        Background = Ux.CardBrush,
        BorderBrush = Ux.AccentBrush,
        BorderThickness = new Thickness(1),
        CornerRadius = new CornerRadius(Ux.Radius),
        Padding = new Thickness(12),
        Margin = new Thickness(0, 0, 0, 8),
        IsVisible = false,
    };

    internal static readonly string[] QuickStartLines =
    {
        "Ask about the open behaviour, its graph, animations or the project.",
        "Choose a model with the Model button; the gear holds the provider and API key.",
        "Type / for commands: /new, /clear, /model, /cancel, /help.",
        "Enter sends and Shift+Enter adds a line; you can queue the next message while it works.",
        "Any edit BGS proposes waits for your approval before it is applied.",
    };
    private readonly StackPanel _commandPanel = new() { Spacing = 1 };
    private readonly List<Border> _commandRows = new();
    private readonly List<AssistantCommand> _commandChoices = new();
    private int _commandChoice = -1;
    private readonly TextBlock _progressText = new()
    {
        TextWrapping = TextWrapping.Wrap,
        Foreground = Ux.MetaBrush,
        FontSize = Ux.FontBody,
    };
    private readonly Border _progress;
    private readonly DispatcherTimer _progressTimer = new() { Interval = TimeSpan.FromSeconds(1) };
    private string _progressPhase = "";
    private DateTime _progressStarted;
    private readonly TextBox _composer = new()
    {
        AcceptsReturn = true,
        TextWrapping = TextWrapping.Wrap,
        MinHeight = 54,
        MaxHeight = 140,
    };
    private readonly Button _modelButton = Ux.Secondary("Model \u25BE");
    private readonly AssistantModelPicker _picker = new();
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
        _conversation = conversation;

        _progress = new Border
        {
            Background = Ux.BaseBrush,
            BorderBrush = Ux.BorderBrush,
            BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(Ux.Radius),
            Padding = new Thickness(10),
            IsVisible = false,
            Child = new StackPanel
            {
                Spacing = 3,
                Children =
                {
                    new TextBlock
                    {
                        Text = "Assistant",
                        Foreground = Ux.TitleBrush,
                        FontSize = Ux.FontSmall,
                        FontWeight = FontWeight.SemiBold,
                    },
                    _progressText,
                },
            },
        };
        _messages.Children.Add(_progress);
        _progressTimer.Tick += (_, _) =>
        {
            if (!_progress.IsVisible) return;
            int seconds = (int)(DateTime.UtcNow - _progressStarted).TotalSeconds;
            _progressText.Text = seconds >= 3 ? _progressPhase + "  " + seconds + "s" : _progressPhase;
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
        _modelReset.Click += (_, _) => ModelResetRequested?.Invoke();

        _modelButton.FontSize = Ux.FontSmall;
        _modelButton.Padding = new Thickness(10, 3, 10, 3);
        _modelButton.IsVisible = false;
        ToolTip.SetTip(_modelButton, "Choose the model for the next request");
        _picker.IsVisible = false;
        _modelButton.Click += (_, _) =>
        {
            _picker.IsVisible = !_picker.IsVisible;
            if (_picker.IsVisible)
            {
                _picker.FocusSearch();
                ModelControlOpened?.Invoke();
            }
        };
        _picker.Chosen += model =>
        {
            _picker.IsVisible = false;
            ModelSelected?.Invoke(model);
        };
        _picker.Dismissed += () => _picker.IsVisible = false;

        _send.Click += (_, _) => SendComposer();
        _cancel.Click += (_, _) => CancelRequested?.Invoke();
        _cancel.IsVisible = false;
        _composer.AddHandler(KeyDownEvent, OnComposerKeyDown, RoutingStrategies.Tunnel);
        _composer.TextChanged += (_, _) => RefreshCommands();
        _commandPanel.IsVisible = false;

        var toolbar = new DockPanel { LastChildFill = true, Margin = new Thickness(0, 6, 0, 0) };
        DockPanel.SetDock(_send, Dock.Right);
        DockPanel.SetDock(_cancel, Dock.Right);
        DockPanel.SetDock(_modelReset, Dock.Right);
        DockPanel.SetDock(_modelButton, Dock.Right);
        toolbar.Children.Add(_send);
        toolbar.Children.Add(_cancel);
        toolbar.Children.Add(_modelReset);
        toolbar.Children.Add(_modelButton);
        toolbar.Children.Add(_status);

        var quickStartDismiss = Ux.Primary("Got it");
        quickStartDismiss.Click += (_, _) => QuickStartDismissed?.Invoke();
        var quickStartBody = new StackPanel { Spacing = 6 };
        quickStartBody.Children.Add(new TextBlock
        {
            Text = "Quick start",
            Foreground = Ux.TitleBrush,
            FontSize = Ux.FontBody,
            FontWeight = FontWeight.SemiBold,
        });
        foreach (string line in QuickStartLines)
        {
            quickStartBody.Children.Add(new TextBlock
            {
                Text = "\u2022  " + line,
                Foreground = Ux.MetaBrush,
                FontSize = Ux.FontSmall,
                TextWrapping = TextWrapping.Wrap,
            });
        }
        quickStartBody.Children.Add(quickStartDismiss);
        _quickStart.Child = quickStartBody;

        var bottom = new StackPanel
        {
            Spacing = 0,
            Children = { _quickStart, _approval, _picker, _queuePanel, _commandPanel, _composer, toolbar },
        };

        var body = new Grid();
        body.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
        body.RowDefinitions.Add(new RowDefinition(new GridLength(1, GridUnitType.Star)));
        body.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
        Grid.SetRow(header, 0);
        Grid.SetRow(_conversation, 1);
        Grid.SetRow(bottom, 2);
        body.Children.Add(header);
        body.Children.Add(_conversation);
        body.Children.Add(bottom);
        Child = body;
    }

    public event Action<string>? SendRequested;
    public event Action? CancelRequested;
    public event Action? NewChatRequested;
    public event Action? CloseRequested;
    public event Action? ApproveRequested;
    public event Action? RejectRequested;
    public event Action<CodexModel>? ModelSelected;
    public event Action? ModelResetRequested;
    public event Action? ModelControlOpened;
    public event Action<int>? QueuedRemoved;
    public event Action? QuickStartDismissed;

    public bool QuickStartVisible => _quickStart.IsVisible;
    public void ShowQuickStart() => _quickStart.IsVisible = true;
    public void HideQuickStart() => _quickStart.IsVisible = false;

    public int MessageCount => _messageCount;
    public bool IsBusy => !_send.IsVisible;
    public bool ModelSelectorVisible => _modelButton.IsVisible;
    public bool ModelResetVisible
    {
        get => _modelReset.IsVisible;
        internal set => _modelReset.IsVisible = value;
    }
    public string? SelectedModelItem => _picker.SelectedModel.Length > 0 ? _picker.SelectedModel : null;
    public bool ModelPanelOpen => _picker.IsVisible;
    internal AssistantModelPicker ModelPicker => _picker;
    internal Button ModelButton => _modelButton;
    internal string ModelButtonLabel => _modelButton.Content?.ToString() ?? "";
    internal void FavoriteForTest(string wireModel) => _picker.InvokeFavorite(wireModel);
    internal Button ModelReset => _modelReset;
    internal void CloseModelPopup() => _picker.IsVisible = false;
    internal void InvokeModelSelected(CodexModel model) => ModelSelected?.Invoke(model);
    internal void InvokeModelResetRequested() => ModelResetRequested?.Invoke();

    public void SetModels(IReadOnlyList<CodexModel> models, string selected, string defaultLabel)
    {
        _picker.SetModels(models, selected);
        _modelButton.IsVisible = models.Count > 0;
        _modelReset.IsVisible = selected.Length > 0;
        CodexModel? chosen = models.FirstOrDefault(model =>
            string.Equals(model.WireModel, selected, StringComparison.Ordinal));
        _modelButton.Content = (chosen?.DisplayName ?? defaultLabel) + " \u25BE";
    }
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
        _send.IsVisible = !busy;
        _cancel.IsVisible = busy;
        _modelButton.IsEnabled = !busy;
        _modelReset.IsEnabled = !busy;
    }
    public void SetApproval(bool visible) => _approval.IsVisible = visible;

    public bool ProgressVisible => _progress.IsVisible;
    public string ProgressText => _progressText.Text ?? "";

    public void ShowProgress(string text)
    {
        _progressPhase = text;
        _progressStarted = DateTime.UtcNow;
        _progressText.Text = text;
        _progress.IsVisible = true;
        if (_messages.Children.Count == 0 || !ReferenceEquals(_messages.Children[^1], _progress))
        {
            _messages.Children.Remove(_progress);
            _messages.Children.Add(_progress);
        }
        if (!_progressTimer.IsEnabled) _progressTimer.Start();
        ScrollToEnd();
    }

    public void HideProgress()
    {
        _progressTimer.Stop();
        _progress.IsVisible = false;
    }

    public void Clear()
    {
        _messages.Children.Clear();
        _messages.Children.Add(_progress);
        HideProgress();
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

    private void OnComposerKeyDown(object? sender, KeyEventArgs args)
    {
        if (_commandPanel.IsVisible)
        {
            switch (args.Key)
            {
                case Key.Escape:
                    args.Handled = true;
                    HideCommands();
                    return;
                case Key.Down:
                    args.Handled = true;
                    MoveCommand(1);
                    return;
                case Key.Up:
                    args.Handled = true;
                    MoveCommand(-1);
                    return;
                case Key.Tab:
                    args.Handled = true;
                    PickCommand();
                    return;
                case Key.Enter:
                    if (!TypedCommandMatchesExactly())
                    {
                        args.Handled = true;
                        PickCommand();
                        return;
                    }
                    break;
            }
        }
        if (args.Key != Key.Enter) return;
        if ((args.KeyModifiers & KeyModifiers.Shift) != 0) return;
        args.Handled = true;
        SendComposer();
    }

    internal Func<IReadOnlyList<AssistantCommand>>? CommandSource { get; set; }

    internal bool CommandsVisible => _commandPanel.IsVisible;
    internal IReadOnlyList<AssistantCommand> CommandChoices => _commandChoices;

    private string? TypedCommandName()
    {
        string text = _composer.Text ?? "";
        if (text.Length == 0 || text[0] != '/') return null;
        if (text.Contains(' ')) return null;
        return text[1..];
    }

    private void RefreshCommands()
    {
        IReadOnlyList<AssistantCommand>? source = CommandSource?.Invoke();
        string? typed = TypedCommandName();
        if (source is null || typed is null)
        {
            HideCommands();
            return;
        }

        _commandChoices.Clear();
        _commandRows.Clear();
        _commandPanel.Children.Clear();
        foreach (AssistantCommand command in source)
        {
            if (_commandChoices.Count >= 8) break;
            if (!command.Name.StartsWith(typed, StringComparison.OrdinalIgnoreCase)) continue;
            _commandChoices.Add(command);
        }
        if (_commandChoices.Count == 0)
        {
            HideCommands();
            return;
        }

        for (int index = 0; index < _commandChoices.Count; index++)
        {
            AssistantCommand command = _commandChoices[index];
            var label = new TextBlock
            {
                Text = "/" + command.Name,
                Foreground = Ux.CodeBrush,
                FontSize = Ux.FontSmall,
                FontWeight = FontWeight.SemiBold,
                VerticalAlignment = VerticalAlignment.Center,
            };
            var row = new Border
            {
                Background = Brushes.Transparent,
                CornerRadius = new CornerRadius(Ux.Radius),
                Padding = new Thickness(6, 3, 6, 3),
                Cursor = new Cursor(StandardCursorType.Hand),
                Child = new StackPanel
                {
                    Orientation = Orientation.Horizontal,
                    Spacing = 8,
                    Children =
                    {
                        label,
                        new TextBlock
                        {
                            Text = command.Description,
                            Foreground = Ux.MutedBrush,
                            FontSize = Ux.FontSmall,
                            VerticalAlignment = VerticalAlignment.Center,
                        },
                    },
                },
            };
            AssistantCommand captured = command;
            row.PointerPressed += (_, args) =>
            {
                args.Handled = true;
                AcceptCommand(captured);
            };
            _commandRows.Add(row);
            _commandPanel.Children.Add(row);
        }

        _commandChoice = 0;
        PaintCommands();
        _commandPanel.IsVisible = true;
    }

    private void AcceptCommand(AssistantCommand command)
    {
        _composer.Text = "/" + command.Name + " ";
        _composer.CaretIndex = (_composer.Text ?? "").Length;
        HideCommands();
    }

    private void PickCommand()
    {
        if (_commandChoice < 0 || _commandChoice >= _commandChoices.Count) return;
        AcceptCommand(_commandChoices[_commandChoice]);
    }

    private bool TypedCommandMatchesExactly()
    {
        string? typed = TypedCommandName();
        if (typed is null) return false;
        foreach (AssistantCommand command in _commandChoices)
            if (string.Equals(command.Name, typed, StringComparison.OrdinalIgnoreCase)) return true;
        return false;
    }

    private void MoveCommand(int delta)
    {
        if (_commandChoices.Count == 0) return;
        _commandChoice = (_commandChoice + delta + _commandChoices.Count) % _commandChoices.Count;
        PaintCommands();
    }

    private void PaintCommands()
    {
        for (int index = 0; index < _commandRows.Count; index++)
            _commandRows[index].Background = index == _commandChoice
                ? Ux.CardHoverBrush
                : Brushes.Transparent;
    }

    private void HideCommands()
    {
        _commandPanel.IsVisible = false;
        _commandChoices.Clear();
        _commandRows.Clear();
        _commandPanel.Children.Clear();
        _commandChoice = -1;
    }

    public void OpenModelPicker()
    {
        _picker.IsVisible = true;
        _picker.FocusSearch();
        ModelControlOpened?.Invoke();
    }

    public void ShowQueue(IReadOnlyList<string> items)
    {
        _queuePanel.Children.Clear();
        for (int index = 0; index < items.Count; index++)
        {
            int captured = index;
            var text = new TextBlock
            {
                Text = "Queued: " + items[index],
                Foreground = Ux.MutedBrush,
                FontSize = Ux.FontSmall,
                TextWrapping = TextWrapping.Wrap,
                VerticalAlignment = VerticalAlignment.Center,
            };
            var remove = new Button
            {
                Content = "\u00D7",
                Background = Brushes.Transparent,
                BorderThickness = new Thickness(0),
                Foreground = Ux.MutedBrush,
                FontSize = 12,
                Padding = new Thickness(6, 0, 6, 0),
            };
            remove.Click += (_, _) => QueuedRemoved?.Invoke(captured);
            var row = new DockPanel { LastChildFill = true };
            DockPanel.SetDock(remove, Dock.Right);
            row.Children.Add(remove);
            row.Children.Add(text);
            _queuePanel.Children.Add(new Border
            {
                Background = Ux.CardBrush,
                BorderBrush = Ux.BorderBrush,
                BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(Ux.Radius),
                Padding = new Thickness(8, 4, 4, 4),
                Margin = new Thickness(0, 0, 0, 4),
                Child = row,
            });
        }
        _queuePanel.IsVisible = items.Count > 0;
    }

    private void ScrollToEnd() => Dispatcher.UIThread.Post(() =>
    {
        double bottom = _conversation.Extent.Height;
        if (_conversation.Offset.Y < bottom)
            _conversation.Offset = new Vector(_conversation.Offset.X, bottom);
    }, DispatcherPriority.Background);

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
        var message = new Border
        {
            Background = background,
            BorderBrush = Ux.BorderBrush,
            BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(Ux.Radius),
            Padding = new Thickness(10),
            Child = new StackPanel { Spacing = 3, Children = { label, content } },
        };
        int index = _progress.IsVisible ? Math.Max(0, _messages.Children.Count - 1) : _messages.Children.Count;
        _messages.Children.Insert(index, message);
        _messageCount++;
        ScrollToEnd();
    }
}
