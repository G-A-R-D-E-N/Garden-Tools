using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;

namespace BehaviourStudio.App;

public sealed class AssistantConversationSidebar : Border
{
    public event Action? NewChatRequested;
    public event Action<string>? ConversationSelected;
    public event Action<string>? DeleteRequested;
    public event Action<string, string>? RenameCommitted;

    private readonly StackPanel _items = new() { Spacing = 2 };
    private readonly TextBlock _empty = new()
    {
        Text = "No saved chats yet.\nStart one above.",
        Foreground = Ux.MutedBrush,
        FontSize = Ux.FontSmall,
        TextWrapping = TextWrapping.Wrap,
        Margin = new Thickness(0, 6, 0, 0),
    };
    private readonly Dictionary<string, TextBox> _editingEditors = new(StringComparer.Ordinal);
    private readonly Dictionary<string, TextBlock> _editingLabels = new(StringComparer.Ordinal);

    public AssistantConversationSidebar()
    {
        Background = Ux.RailBrush;
        BorderBrush = Ux.BorderBrush;
        BorderThickness = new Thickness(0, 0, 1, 0);
        Padding = new Thickness(8);

        var header = new TextBlock
        {
            Text = "Conversations",
            Foreground = Ux.TitleBrush,
            FontSize = Ux.FontSmall,
            FontWeight = FontWeight.SemiBold,
            Margin = new Thickness(0, 0, 0, 4),
        };

        var newChat = Ux.Secondary("+ New chat");
        newChat.HorizontalAlignment = HorizontalAlignment.Stretch;
        newChat.Click += (_, _) => NewChatRequested?.Invoke();

        var hint = new TextBlock
        {
            Text = "Each chat stays isolated. Switch at any time.",
            Foreground = Ux.MutedBrush,
            FontSize = Ux.FontSmall,
            TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(0, 4, 0, 0),
        };

        var stack = new StackPanel();
        stack.Children.Add(header);
        stack.Children.Add(newChat);
        stack.Children.Add(hint);
        stack.Children.Add(_empty);
        var scroll = new ScrollViewer
        {
            VerticalScrollBarVisibility = Avalonia.Controls.Primitives.ScrollBarVisibility.Auto,
            HorizontalScrollBarVisibility = Avalonia.Controls.Primitives.ScrollBarVisibility.Disabled,
            Content = _items,
            Margin = new Thickness(0, 8, 0, 0),
        };
        stack.Children.Add(scroll);
        Child = stack;
    }

    public void Bind(IReadOnlyList<AssistantChatSummary> chats, string? activeChatId)
    {
        _items.Children.Clear();
        _editingEditors.Clear();
        _editingLabels.Clear();
        _empty.IsVisible = chats.Count == 0;
        foreach (AssistantChatSummary summary in chats)
            _items.Children.Add(BuildRow(summary, activeChatId));
    }

    private Border BuildRow(AssistantChatSummary summary, string? activeChatId)
    {
        bool active = activeChatId is not null && string.Equals(activeChatId, summary.Id, StringComparison.Ordinal);
        var label = new TextBlock
        {
            Text = summary.Title,
            Foreground = active ? Ux.TitleBrush : Ux.MetaBrush,
            FontSize = Ux.FontSmall,
            FontWeight = active ? FontWeight.SemiBold : FontWeight.Normal,
            TextWrapping = TextWrapping.Wrap,
            VerticalAlignment = VerticalAlignment.Center,
        };
        var editor = new TextBox
        {
            Text = summary.Title,
            IsVisible = false,
            Margin = new Thickness(0, 0, 0, 0),
            FontSize = Ux.FontSmall,
            MaxLength = ChatText.MaxTitleLength,
        };
        _editingEditors[summary.Id] = editor;
        _editingLabels[summary.Id] = label;

        var rename = Ux.Secondary("Rename");
        rename.FontSize = Ux.FontSmall;
        rename.Padding = new Thickness(4, 2, 4, 2);
        rename.Click += (_, _) =>
        {
            label.IsVisible = false;
            editor.IsVisible = true;
            editor.Text = summary.Title;
            editor.Focus();
            editor.SelectAll();
        };

        var delete = Ux.Secondary("Delete");
        delete.FontSize = Ux.FontSmall;
        delete.Padding = new Thickness(4, 2, 4, 2);
        delete.Click += (_, _) => DeleteRequested?.Invoke(summary.Id);

        var select = Ux.Primary("Open");
        select.FontSize = Ux.FontSmall;
        select.Padding = new Thickness(4, 2, 4, 2);
        select.IsEnabled = !active;
        select.Click += (_, _) => ConversationSelected?.Invoke(summary.Id);

        editor.KeyDown += (_, args) =>
        {
            if (args.Key == Avalonia.Input.Key.Enter)
                CommitEditor(editor, summary.Id, label);
            else if (args.Key == Avalonia.Input.Key.Escape)
                CancelEditor(editor, label, summary.Title);
        };
        editor.LostFocus += (_, _) => CommitEditor(editor, summary.Id, label);

        var buttonRow = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Spacing = 4,
            Margin = new Thickness(0, 2, 0, 0),
        };
        buttonRow.Children.Add(rename);
        buttonRow.Children.Add(delete);
        buttonRow.Children.Add(select);

        var content = new StackPanel
        {
            Spacing = 2,
            Children = { label, editor, buttonRow },
        };

        var border = new Border
        {
            Background = active ? Ux.CardBrush : Avalonia.Media.Brushes.Transparent,
            BorderBrush = active ? Ux.BorderBrush : Avalonia.Media.Brushes.Transparent,
            BorderThickness = new Thickness(active ? 1 : 0),
            CornerRadius = new CornerRadius(3),
            Padding = new Thickness(6),
            Margin = new Thickness(0, 0, 0, 4),
            Child = content,
        };
        return border;
    }

    private void CommitEditor(TextBox editor, string chatId, TextBlock label)
    {
        if (!editor.IsVisible) return;
        string text = (editor.Text ?? "").Trim();
        editor.IsVisible = false;
        if (text.Length == 0)
        {
            label.IsVisible = true;
            return;
        }
        label.IsVisible = true;
        RenameCommitted?.Invoke(chatId, text);
    }

    private static void CancelEditor(TextBox editor, TextBlock label, string previous)
    {
        editor.Text = previous;
        editor.IsVisible = false;
        label.IsVisible = true;
    }
}
