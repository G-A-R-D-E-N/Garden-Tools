using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using BehaviourStudio.App;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class MultiChatControllerTests : IDisposable
{
    private readonly string _folder;
    private readonly string _previousFolder;

    public MultiChatControllerTests()
    {
        _folder = Path.Combine(Path.GetTempPath(), "bgs-assistant-ctrl-" + Guid.NewGuid().ToString("N"));
        _previousFolder = AssistantChatStore.Folder;
        AssistantChatStore.Folder = _folder;
    }

    public void Dispose() => AssistantChatStore.Folder = _previousFolder;

    [Fact]
    public void ControllerSeedsInitialChatWhenNoneSaved()
    {
        var factory = MakeFactory();
        using var controller = new AssistantConversationController(factory);

        Assert.NotNull(controller.Active);
        Assert.NotNull(controller.Active!.Chat);
        Assert.NotNull(controller.List.ActiveChatId);
        Assert.True(AssistantChatStore.Exists(controller.Active.Chat.Id));
    }

    [Fact]
    public void ControllerRestoresPreviouslySavedChatsOnRestart()
    {
        AssistantChat savedA = Build("alpha", "alpha-1");
        AssistantChat savedB = Build("beta", "beta-1");
        AssistantChatStore.Save(savedA);
        AssistantChatStore.Save(savedB);

        var factory = MakeFactory();
        using var controller = new AssistantConversationController(factory,
            initialChats: new[] { savedA, savedB },
            activeChatId: savedB.Id);

        Assert.Equal(2, controller.List.Count);
        Assert.Equal(savedB.Id, controller.List.ActiveChatId);
    }

    [Fact]
    public void NewChatCreatesAndSelectsAFreshConversation()
    {
        var factory = MakeFactory();
        using var controller = new AssistantConversationController(factory);

        AssistantConversationEntry? before = controller.Active;
        AssistantConversationEntry? created = controller.NewChat();

        Assert.NotNull(created);
        Assert.NotNull(before);
        Assert.NotEqual(before!.Chat.Id, created!.Chat.Id);
        Assert.Equal(created.Chat.Id, controller.Active!.Chat.Id);
        Assert.Equal("New chat", created.Chat.Title);
    }

    [Fact]
    public void SwitchAndBackReturnsOriginalTranscript()
    {
        var factory = new Dictionary<string, Func<string, IAssistantSession?>>
        {
            [""] = _ => new FixedReplySession("hello world"),
        };
        Func<string, IAssistantSession?> sessionFactory =
            id => factory.TryGetValue(id, out var f) ? f(id)
                 : (factory.TryGetValue("", out var def) ? def(id) : null);
        using var controller = new AssistantConversationController(sessionFactory);

        AssistantConversationEntry a = controller.Active!;
        AssistantConversationEntry b = controller.NewChat()!;
        Assert.Equal(b.Chat.Id, controller.Active!.Chat.Id);

        Assert.True(controller.TrySelectById(a.Chat.Id));

        Assert.Equal(a.Chat.Id, controller.List.ActiveChatId);
    }

    [Fact]
    public void RenameUpdatesTitleAndPersists()
    {
        var factory = MakeFactory();
        using var controller = new AssistantConversationController(factory);
        AssistantConversationEntry? entry = controller.Active;

        Assert.True(controller.Rename(controller.List.ActiveIndex, "Renamed"));

        AssistantChat? loaded = AssistantChatStore.Load(entry!.Chat.Id);
        Assert.NotNull(loaded);
        Assert.Equal("Renamed", loaded!.Title);
    }

    [Fact]
    public void DeleteRemovesFromDisk()
    {
        var factory = MakeFactory();
        using var controller = new AssistantConversationController(factory);

        AssistantConversationEntry a = controller.Active!;
        AssistantConversationEntry b = controller.NewChat()!;

        Assert.True(controller.TrySelectById(b.Chat.Id));
        int idxB = controller.List.IndexOf(b.Chat.Id);
        Assert.True(controller.TrySelectById(a.Chat.Id));

        AssistantConversationEntry? after = controller.Delete(idxB);
        Assert.NotNull(after);
        Assert.False(AssistantChatStore.Exists(b.Chat.Id));
    }

    [Fact]
    public void DeleteFinalChatLeavesAtLeastOneEntry()
    {
        var factory = MakeFactory();
        using var controller = new AssistantConversationController(factory);

        AssistantConversationEntry? after = controller.Delete(controller.List.ActiveIndex);
        Assert.NotNull(after);
        Assert.True(controller.List.Count >= 1);
        Assert.NotNull(controller.Active);
    }

    [Fact]
    public void MaxConversationsIsEnforced()
    {
        var factory = MakeFactory();
        using var controller = new AssistantConversationController(factory);
        for (int i = 0; i < AssistantChatStore.MaxConversations - 1; i++)
            controller.NewChat();

        Assert.Equal(AssistantChatStore.MaxConversations, controller.List.Count);

        for (int i = 0; i < 8; i++) controller.NewChat();

        Assert.Equal(AssistantChatStore.MaxConversations, controller.List.Count);
    }

    [Fact]
    public async Task SendPersistsUserAndAssistantMessages()
    {
        var registered = new Dictionary<string, IAssistantSession>();
        Func<string, IAssistantSession?> factory = chatId =>
        {
            if (registered.TryGetValue(chatId, out var session)) return session;
            var created = new FixedReplySession("ok-from-model", tools: new[]
            {
                new AssistantToolActivity("bgs.inspect_behavior", false),
            });
            registered[chatId] = created;
            return created;
        };
        using var controller = new AssistantConversationController(factory);
        AssistantConversationEntry entry = controller.Active!;

        AssistantReply reply = await controller.SendAsync("hello", CodexSessionFixture.Context())
            .ConfigureAwait(false);
        Assert.Equal("ok", reply.Status);

        AssistantChat? loaded = AssistantChatStore.Load(entry.Chat.Id);
        Assert.NotNull(loaded);
        Assert.True(loaded!.Messages.Count >= 3,
            "user + assistant + tool messages should be persisted");
        Assert.Contains(loaded.Messages, m => m.Role == AssistantChatRole.User);
        Assert.Contains(loaded.Messages, m => m.Role == AssistantChatRole.Assistant);
    }

    [Fact]
    public async Task ReplayPromptContainsBoundedPriorConversation()
    {
        var recorded = new List<string>();
        var sessions = new Dictionary<string, IAssistantSession>();
        Func<string, IAssistantSession?> factory = chatId =>
        {
            if (sessions.TryGetValue(chatId, out var session)) return session;
            var created = new RecordingReplySession(replied =>
            {
                recorded.Add(replied);
                return new AssistantReply("ok", "reply", 0, Array.Empty<AssistantToolActivity>(), false);
            });
            sessions[chatId] = created;
            return created;
        };

        using var controller = new AssistantConversationController(factory);
        AssistantConversationEntry entry = controller.Active!;

        AssistantChat seeded = entry.Chat;
        DateTime stamp = DateTime.UtcNow.AddMinutes(-10);
        seeded = AssistantChatStore.AppendMessage(seeded,
            new AssistantChatMessage(AssistantChatRole.User, "first question", stamp));
        seeded = AssistantChatStore.AppendMessage(seeded,
            new AssistantChatMessage(AssistantChatRole.Assistant, "first answer", stamp.AddSeconds(1)));
        AssistantChatStore.Save(seeded);
        entry.Chat = seeded;

        await controller.SendAsync("second question", CodexSessionFixture.Context())
            .ConfigureAwait(false);

        Assert.Single(recorded);
        string prompt = recorded[0];
        Assert.Contains("first question", prompt);
        Assert.Contains("first answer", prompt);
        Assert.Contains("second question", prompt);
        Assert.DoesNotContain("tool", prompt, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void SwitchRefusedWhileBusy()
    {
        var factory = MakeFactory();
        using var controller = new AssistantConversationController(factory);
        controller.List.IsBusy = true;

        AssistantConversationEntry a = controller.Active!;
        _ = controller.NewChat();

        Assert.False(controller.TrySelectById(a.Chat.Id));
    }

    [Fact]
    public void ApproveClearsPendingApprovalAndNeverRestoresIt()
    {
        var factory = MakeFactory();
        using var controller = new AssistantConversationController(factory);
        AssistantConversationEntry entry = controller.Active!;
        entry.MarkPendingApproval();

        controller.ApproveAndContinue(entry);
        Assert.False(entry.ApprovalActive);

        entry = controller.NewChat()!;
        Assert.False(entry.ApprovalActive,
            "switching/NewChat must not carry approval state between conversations");
    }

    private static Func<string, IAssistantSession?> MakeFactory() =>
        _ => new FixedReplySession("hello world");

    private AssistantChat Build(string title, string firstMessage)
    {
        AssistantChat chat = AssistantChatStore.CreateEmpty();
        chat = chat.WithTitle(title);
        chat = AssistantChatStore.AppendMessage(chat,
            new AssistantChatMessage(AssistantChatRole.User, firstMessage, DateTime.UtcNow));
        return chat;
    }

    private sealed class FixedReplySession : IAssistantSession
    {
        public FixedReplySession(string text,
            IReadOnlyList<AssistantToolActivity>? tools = null)
        {
            Text = text;
            Tools = tools ?? Array.Empty<AssistantToolActivity>();
        }

        public string Text { get; }
        public IReadOnlyList<AssistantToolActivity> Tools { get; }

        public Task<AssistantReply> SendAsync(string prompt, AssistantContext context, CancellationToken cancellationToken = default)
            => Task.FromResult(new AssistantReply("ok", Text, 1, Tools, false));

        public void ClearHistory() { }

        public void Dispose() { }
    }

    private sealed class RecordingReplySession : IAssistantSession
    {
        private readonly Func<string, AssistantReply> _produce;
        public RecordingReplySession(Func<string, AssistantReply> produce) => _produce = produce;

        public Task<AssistantReply> SendAsync(string prompt, AssistantContext context, CancellationToken cancellationToken = default)
            => Task.FromResult(_produce(prompt));

        public void ClearHistory() { }
        public void Dispose() { }
    }
}
