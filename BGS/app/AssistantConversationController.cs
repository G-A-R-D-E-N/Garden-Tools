using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace BehaviourStudio.App;

public sealed class AssistantConversationController : IDisposable
{
    private readonly Func<string, IAssistantSession?> _sessionFactory;

    public AssistantConversationController(
        Func<string, IAssistantSession?> sessionFactory,
        IEnumerable<AssistantChat>? initialChats = null,
        string? activeChatId = null)
    {
        _sessionFactory = sessionFactory ?? throw new ArgumentNullException(nameof(sessionFactory));
        List = new AssistantConversationList();
        IEnumerable<AssistantChat> restoredChats = initialChats ?? Array.Empty<AssistantChat>();
        foreach (AssistantChat chat in restoredChats.OrderByDescending(c => c.UpdatedUtc))
            List.Add(chat);

        if (List.Count == 0)
        {
            var empty = AssistantChatStore.CreateEmpty();
            AssistantChatStore.Save(empty);
            List.Add(empty);
            activeChatId = empty.Id;
        }
        else if (string.IsNullOrEmpty(activeChatId) || List.IndexOf(activeChatId) < 0)
        {
            activeChatId = List.Entries[0].Chat.Id;
        }

        if (List.Entries.Count == 0 || !List.TrySetActiveById(activeChatId))
        {
            throw new InvalidOperationException("Could not seed an initial chat.");
        }
    }

    public AssistantConversationList List { get; }
    public AssistantConversationEntry? Active => List.Active;
    public bool IsBusy => List.IsBusy;

    public IAssistantSession? ActiveSession => List.Active?.Session;

    public bool TrySelect(int index) => List.Select(index) != null;
    public bool TrySelectById(string chatId) => List.TrySetActiveById(chatId);

    public AssistantConversationEntry? NewChat() => List.NewChat();

    public bool Rename(int index, string newTitle) => List.Rename(index, newTitle);

    public AssistantConversationEntry? Delete(int index) => List.Delete(index);

    public async Task<AssistantReply> SendAsync(
        string prompt, AssistantContext context, CancellationToken cancellationToken = default)
    {
        AssistantConversationEntry? entry = List.Active;
        if (entry is null) return BusyReply();

        if (string.IsNullOrWhiteSpace(prompt)) return new("error", "A message is required.",
            0, Array.Empty<AssistantToolActivity>(), entry.ApprovalActive);

        string replayPrompt = ComposeReplay(entry.Chat, prompt);
        DateTime stamp = DateTime.UtcNow;
        AssistantChat updated = AssistantChatStore.AppendMessage(entry.Chat,
            new AssistantChatMessage(AssistantChatRole.User,
                ChatText.Cap(prompt), stamp));

        IAssistantSession? session = entry.Session;
        if (session is null)
        {
            session = _sessionFactory(entry.Chat.Id);
            if (session is null) return new("error",
                "BGS could not start an assistant session. Sign in with ChatGPT and try again.",
                0, Array.Empty<AssistantToolActivity>(), entry.ApprovalActive);
            entry.Session = session;
        }

        AssistantReply reply;
        try
        {
            reply = await session
                .SendAsync(replayPrompt, context, cancellationToken)
                .ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            return new("cancelled", "The request was cancelled.", 0,
                Array.Empty<AssistantToolActivity>(), entry.ApprovalActive);
        }

        DateTime replyStamp = DateTime.UtcNow;
        updated = AssistantChatStore.AppendMessage(updated,
            new AssistantChatMessage(AssistantChatRole.Assistant,
                ChatText.Cap(reply.Text), replyStamp));
        foreach (AssistantToolActivity tool in reply.Tools)
        {
            string status = tool.RequiresApproval ? "pending approval" : "ran";
            updated = AssistantChatStore.AppendMessage(updated,
                new AssistantChatMessage(AssistantChatRole.Tool,
                    $"{tool.Name} ({status})", replyStamp,
                    ToolName: tool.Name, ToolStatus: status, ToolRequiresApproval: tool.RequiresApproval));
        }

        if (reply.AwaitingApproval) entry.MarkPendingApproval();
        else entry.ClearPendingApproval();

        entry.Chat = updated;
        AssistantChatStore.Save(updated);
        return reply;
    }

    public void ApproveAndContinue(AssistantConversationEntry? entry)
    {
        if (entry is null) return;
        entry.ClearPendingApproval();
    }

    public void RejectAndClear(AssistantConversationEntry? entry)
    {
        if (entry is null) return;
        entry.ClearPendingApproval();
    }

    public void Cancel(AssistantConversationEntry? entry)
    {
        entry?.Session?.Dispose();
        if (entry is not null) entry.Session = null;
    }

    public void Dispose() => List.Dispose();

    internal static string ComposeReplay(AssistantChat chat, string newUserPrompt)
    {
        var turns = new List<AssistantChatMessage>();
        int turnsAdded = 0;
        for (int i = chat.Messages.Count - 1; i >= 0; i--)
        {
            AssistantChatMessage m = chat.Messages[i];
            if (m.Role == AssistantChatRole.Tool) continue;
            turns.Insert(0, m);
            if (m.Role == AssistantChatRole.User || m.Role == AssistantChatRole.Assistant) turnsAdded++;
            if (turnsAdded >= AssistantChatStore.MaxReplayTurns) break;
        }

        if (turns.Count == 0) return newUserPrompt;

        var joined = new System.Text.StringBuilder();
        joined.AppendLine("# BGS chat history (most recent last)");
        foreach (AssistantChatMessage m in turns)
        {
            string label = m.Role switch
            {
                AssistantChatRole.User => "user",
                AssistantChatRole.Assistant => "assistant",
                _ => m.Role.ToString().ToLowerInvariant(),
            };
            joined.AppendLine($"[{label}] {ChatText.Cap(m.Text)}");
        }
        joined.AppendLine();
        joined.AppendLine("# Current message");
        joined.Append(newUserPrompt);
        return joined.ToString();
    }

    private static AssistantReply BusyReply() => new(
        "busy",
        "BGS is processing another request.",
        0,
        Array.Empty<AssistantToolActivity>(),
        AwaitingApproval: false);
}
