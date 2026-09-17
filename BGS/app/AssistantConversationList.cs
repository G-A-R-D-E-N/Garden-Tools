using System;
using System.Collections.Generic;
using System.Linq;

namespace BehaviourStudio.App;

public sealed class AssistantConversationEntry : IDisposable
{
    internal AssistantConversationEntry(AssistantChat chat)
    {
        Chat = chat;
    }

    public AssistantChat Chat { get; internal set; }
    public IAssistantSession? Session { get; internal set; }
    public bool PendingApproval { get; set; }
    public Func<bool>? HasPendingApproval { get; internal set; }
    internal bool SessionPrimed { get; set; }
    internal bool Deleted { get; set; }

    public void ClearPendingApproval() => PendingApproval = false;
    public void MarkPendingApproval() => PendingApproval = true;
    public bool ApprovalActive =>
        !Deleted &&
        (PendingApproval ||
        (HasPendingApproval is { } probe && probe()));

    public void Dispose()
    {
        var session = Session;
        Session = null;
        session?.Dispose();
    }
}

public sealed class AssistantConversationList : IDisposable
{
    private readonly List<AssistantConversationEntry> _entries = new();
    private int _activeIndex = -1;

    public AssistantConversationList() { }

    public event Action<int>? SelectionChanged;
    public event Action? StructureChanged;

    public IReadOnlyList<AssistantConversationEntry> Entries => _entries;
    public AssistantConversationEntry? Active => _activeIndex >= 0 ? _entries[_activeIndex] : null;
    public int ActiveIndex => _activeIndex;
    public bool IsBusy { get; set; }
    public string? ActiveChatId => Active?.Chat.Id;

    private AssistantConversationEntry? _ => Active;

    public void Add(AssistantChat chat, int? selectIndex = null)
    {
        if (!AssistantChatStore.IsValidId(chat.Id))
            throw new ArgumentException("Conversation id is not a valid storage key.", nameof(chat));
        if (Count >= AssistantChatStore.MaxConversations)
            throw new InvalidOperationException(
                $"BGS only retains {AssistantChatStore.MaxConversations} conversations.");
        var entry = new AssistantConversationEntry(chat);
        _entries.Add(entry);
        StructureChanged?.Invoke();
        if (selectIndex is int i)
        {
            _activeIndex = i;
            SelectionChanged?.Invoke(_activeIndex);
        }
        else if (_activeIndex < 0)
        {
            _activeIndex = 0;
            SelectionChanged?.Invoke(_activeIndex);
        }
    }

    public int Count => _entries.Count;

    public int IndexOf(string chatId)
    {
        for (int i = 0; i < _entries.Count; i++)
            if (string.Equals(_entries[i].Chat.Id, chatId, StringComparison.Ordinal))
                return i;
        return -1;
    }

    public AssistantConversationEntry? Select(int index)
    {
        if (index < 0 || index >= _entries.Count) return null;
        if (IsBusy) return null;
        AssistantConversationEntry? previous = Active;
        _activeIndex = index;
        SelectionChanged?.Invoke(_activeIndex);
        return _entries[index];
    }

    public bool TrySetActiveById(string? chatId)
    {
        if (string.IsNullOrEmpty(chatId)) return false;
        int idx = IndexOf(chatId);
        if (idx < 0) return false;
        return Select(idx) != null;
    }

    public AssistantConversationEntry? NewChat()
    {
        if (IsBusy) return null;
        if (Count >= AssistantChatStore.MaxConversations) return null;
        var chat = AssistantChatStore.CreateEmpty();
        AssistantChatStore.Save(chat);
        _entries.Add(new AssistantConversationEntry(chat));
        StructureChanged?.Invoke();
        _activeIndex = _entries.Count - 1;
        SelectionChanged?.Invoke(_activeIndex);
        return Active;
    }

    public bool Rename(int index, string newTitle)
    {
        if (index < 0 || index >= _entries.Count) return false;
        var entry = _entries[index];
        AssistantChat renamed = entry.Chat.WithTitle(newTitle);
        AssistantChatStore.Save(renamed);
        entry.Chat = renamed;
        StructureChanged?.Invoke();
        return true;
    }

    public AssistantConversationEntry? Delete(int index)
    {
        if (index < 0 || index >= _entries.Count) return null;
        var entry = _entries[index];
        if (!AssistantChatStore.Delete(entry.Chat.Id)) return null;
        entry.Deleted = true;
        entry.ClearPendingApproval();
        entry.Dispose();
        _entries.RemoveAt(index);
        StructureChanged?.Invoke();

        bool autoSelect = index == _activeIndex;
        bool shift = index < _activeIndex;

        if (_entries.Count == 0)
        {
            var fresh = AssistantChatStore.CreateEmpty();
            AssistantChatStore.Save(fresh);
            _entries.Add(new AssistantConversationEntry(fresh));
            _activeIndex = 0;
            SelectionChanged?.Invoke(_activeIndex);
            return _entries[0];
        }
        else if (autoSelect)
        {
            _activeIndex = Math.Min(index, _entries.Count - 1);
            SelectionChanged?.Invoke(_activeIndex);
        }
        else if (shift)
        {
            _activeIndex--;
            SelectionChanged?.Invoke(_activeIndex);
        }
        return _entries[_activeIndex];
    }

    public IReadOnlyList<AssistantChatSummary> Summaries() =>
        _entries
            .Select(e => new AssistantChatSummary(e.Chat.Id, e.Chat.Title, e.Chat.UpdatedUtc, e.Chat.Messages.Count))
            .OrderByDescending(summary => summary.UpdatedUtc)
            .ToList();

    public void Dispose()
    {
        foreach (var entry in _entries) entry.Dispose();
        _entries.Clear();
        _activeIndex = -1;
    }
}
