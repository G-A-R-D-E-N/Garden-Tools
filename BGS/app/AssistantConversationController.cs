using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using OpenCommonwealth.Services.Hkx;

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
            AssistantChat empty = AssistantChatStore.CreateEmpty();
            try
            {
                empty = AssistantChatStore.Save(empty);
            }
            catch (Exception exception) when (exception is not OperationCanceledException)
            {
            }
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
    public AssistantMutationGate? Mutations { get; set; }
    public AssistantConversationEntry? Active => List.Active;
    public bool IsBusy => List.IsBusy;

    public IAssistantSession? ActiveSession => List.Active?.Session;

    public bool TrySelect(int index)
    {
        if (index < 0 || index >= List.Count) return false;
        if (List.IsBusy) return false;
        if (List.ActiveIndex == index) return true;
        RejectPendingMutations();
        return List.Select(index) != null;
    }

    public bool TrySelectById(string chatId)
    {
        int index = List.IndexOf(chatId);
        return index >= 0 && TrySelect(index);
    }

    public AssistantConversationEntry? NewChat()
    {
        if (List.IsBusy) return null;
        RejectPendingMutations();
        return List.NewChat();
    }

    public bool Rename(int index, string newTitle) => List.Rename(index, newTitle);

    public AssistantConversationEntry? Delete(int index)
    {
        if (index < 0 || index >= List.Count) return null;
        RejectPendingMutations();
        return List.Delete(index);
    }

    public void RejectPendingMutations()
    {
        foreach (AssistantConversationEntry entry in List.Entries) entry.ClearPendingApproval();
        Mutations?.Reject();
    }

    public void EndAllSessions()
    {
        RejectPendingMutations();
        List.ClearSessions();
    }

    public void RejectPending(AssistantConversationEntry? entry)
    {
        entry?.ClearPendingApproval();
        Mutations?.Reject();
    }

    public ClipAnimationChangeResult ApproveActive(AssistantConversationEntry? entry)
    {
        if (entry is null || entry.Deleted) return AssistantTools.NoPendingResult();
        ClipAnimationChangeResult result =
            Mutations?.Approve(entry.Chat.Id) ?? AssistantTools.NoPendingResult();
        entry.ClearPendingApproval();
        return result;
    }

    public async Task<AssistantReply> SendAsync(
        string prompt, AssistantContext context, CancellationToken cancellationToken = default)
    {
        AssistantConversationEntry? entry = List.Active;
        if (entry is null || entry.Deleted) return BusyReply();

        string trimmed = (prompt ?? "").Trim();
        if (trimmed.Length == 0)
            return new("error", "A message is required.", 0,
                Array.Empty<AssistantToolActivity>(), entry.ApprovalActive);
        if (trimmed.Length > AssistantChatStore.MaxPromptCharacters)
            return new("error",
                $"The message is too long. BGS accepts up to {AssistantChatStore.MaxPromptCharacters} characters.",
                0, Array.Empty<AssistantToolActivity>(), entry.ApprovalActive);

        IAssistantSession? session = entry.Session;
        if (session is null)
        {
            session = _sessionFactory(entry.Chat.Id);
            if (session is null) return new("error",
                "BGS could not start an assistant session. Sign in with ChatGPT and try again.",
                0, Array.Empty<AssistantToolActivity>(), entry.ApprovalActive);
            entry.Session = session;
            entry.SessionPrimed = false;
        }

        bool replay = !entry.SessionPrimed;
        try
        {
            replay = await session.PrepareForSendAsync(cancellationToken).ConfigureAwait(false) || replay;
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            return new("cancelled", "The request was cancelled.", 0,
                Array.Empty<AssistantToolActivity>(), entry.ApprovalActive);
        }
        catch (Exception exception) when (exception is not OperationCanceledException)
        {
        }

        string outgoing = replay ? ComposeReplay(entry.Chat, trimmed) : trimmed;
        DateTime stamp = DateTime.UtcNow;
        AssistantChat updated = AppendUserMessage(entry.Chat, trimmed, stamp);

        AssistantReply reply;
        try
        {
            reply = await session
                .SendAsync(outgoing, context, cancellationToken)
                .ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            return new("cancelled", "The request was cancelled.", 0,
                Array.Empty<AssistantToolActivity>(), entry.ApprovalActive);
        }

        return Persist(entry, reply, updated);
    }

    private AssistantReply Persist(AssistantConversationEntry entry, AssistantReply reply, AssistantChat updated)
    {
        lock (entry.SaveGate)
        {
            if (entry.Deleted) return reply;

            entry.SessionPrimed = ThreadEngaged(reply);

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

            if (reply.AwaitingApproval)
            {
                entry.MarkPendingApproval();
                Mutations?.MarkOwner(entry.Chat.Id);
            }
            else
            {
                entry.ClearPendingApproval();
            }

            AssistantChat sanitized = AssistantChatStore.Redact(updated);
            bool redacted = !ReferenceEquals(sanitized, updated);
            entry.Chat = sanitized;

            try
            {
                AssistantChatStore.Save(sanitized);
            }
            catch (Exception exception) when (exception is not OperationCanceledException)
            {
                return reply with { PersistenceNotice = NotSavedMessage };
            }

            return redacted ? reply with { PersistenceNotice = RedactedMessage } : reply;
        }
    }

    internal const string NotSavedMessage =
        "The assistant replied, but BGS could not save this chat to disk. The transcript stays visible for this session.";

    internal const string RedactedMessage =
        "Saved, but BGS redacted text that looked like a credential so it never reached disk.";

    public void Cancel(AssistantConversationEntry? entry)
    {
        if (entry is null) return;
        entry.Session?.Dispose();
        entry.Session = null;
        entry.SessionPrimed = false;
        entry.ClearPendingApproval();
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

    private static AssistantChat AppendUserMessage(AssistantChat chat, string text, DateTime stamp)
    {
        AssistantChat updated = AssistantChatStore.AppendMessage(chat,
            new AssistantChatMessage(AssistantChatRole.User, ChatText.Cap(text), stamp));
        bool firstUserMessage = updated.Messages.Count(m => m.Role == AssistantChatRole.User) == 1;
        if (firstUserMessage && string.Equals(chat.Title, "New chat", StringComparison.Ordinal))
            updated = updated.WithTitle(ChatText.DeriveTitleFromFirstMessage(text));
        return updated;
    }

    private static bool ThreadEngaged(AssistantReply reply) => reply.Status switch
    {
        "signed_out" or "provider_error" or "error" => false,
        _ => true,
    };

    private static AssistantReply BusyReply() => new(
        "busy",
        "BGS is processing another request.",
        0,
        Array.Empty<AssistantToolActivity>(),
        AwaitingApproval: false);
}
