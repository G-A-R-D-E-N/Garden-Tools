using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using BehaviourStudio.App;
using Xunit;

namespace BehaviourStudio.Tests;

[Collection("AssistantChatStore")]
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

    public void Dispose()
    {
        AssistantChatStore.DeleteForTest = null;
        AssistantChatStore.Folder = _previousFolder;
        try { if (Directory.Exists(_folder)) Directory.Delete(_folder, true); }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException) { }
    }

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
    public void DeleteDisposesTheRemovedSession()
    {
        var session = new TrackableSession("reply");
        using var controller = new AssistantConversationController(_ => session);
        AssistantConversationEntry entry = controller.Active!;
        entry.Session = session;

        AssistantConversationEntry? after = controller.Delete(controller.List.ActiveIndex);

        Assert.NotNull(after);
        Assert.True(session.Disposed, "deleting a chat must dispose its session");
        Assert.Null(entry.Session);
    }

    [Fact]
    public async Task DeletingAChatDisposesAndUnregistersItsRealCodexThread()
    {
        using var connection = new FakeConnection();
        var session = new CodexAssistantSession(connection.Connection,
            new[] { Microsoft.Extensions.AI.AIFunctionFactory.Create(
                () => "ok", "bgs.inspect_behavior", "test") });
        using var controller = new AssistantConversationController(_ => session);
        AssistantConversationEntry entry = controller.Active!;

        await controller.SendAsync("hello", CodexSessionFixture.Context()).ConfigureAwait(false);
        string thread = connection.Server.LastThreadId;
        Assert.False(string.IsNullOrEmpty(thread));
        Assert.Equal("1", connection.Connection.Client.RegisteredToolThreadCount);

        controller.Delete(controller.List.ActiveIndex);

        Assert.Equal("0", connection.Connection.Client.RegisteredToolThreadCount);
        await Assert.ThrowsAsync<ObjectDisposedException>(
            () => session.SendAsync("again", CodexSessionFixture.Context()));

        int expected = connection.Server.ToolResponses + 1;
        connection.Server.EmitServerRequest(CodexMethods.DynamicToolCall, "\"s:700\"",
            "{\"threadId\":\"" + thread +
            "\",\"turnId\":\"turn-x\",\"callId\":\"c-x\",\"tool\":\"bgs_inspect_behavior\",\"arguments\":{}}");
        for (int attempt = 0; attempt < 200 && connection.Server.ToolResponses < expected; attempt++)
            await Task.Delay(10);
        Assert.Equal(expected, connection.Server.ToolResponses);
        Assert.Contains("\"success\":false", connection.Server.LastToolResponse,
            StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task DeleteDuringActiveRequestCannotResurrectHistory()
    {
        var session = new BlockingSession();
        using var controller = new AssistantConversationController(_ => session);
        AssistantConversationEntry entry = controller.Active!;

        Task<AssistantReply> pending = controller.SendAsync("slow", CodexSessionFixture.Context());
        await session.Started.Task.WaitAsync(TimeSpan.FromSeconds(5));

        AssistantConversationEntry? after = controller.Delete(controller.List.ActiveIndex);
        session.Complete();
        AssistantReply reply = await pending;

        Assert.NotNull(after);
        Assert.Equal("ok", reply.Status);
        Assert.False(AssistantChatStore.Exists(entry.Chat.Id),
            "a late completion must not recreate a deleted chat");
    }

    [Fact]
    public void FailedDiskDeleteKeepsTheChatVisible()
    {
        using var controller = new AssistantConversationController(MakeFactory());
        AssistantConversationEntry entry = controller.Active!;
        AssistantChatStore.DeleteForTest = _ => false;
        try
        {
            AssistantConversationEntry? after = controller.Delete(controller.List.ActiveIndex);

            Assert.Null(after);
            Assert.Equal(1, controller.List.Count);
            Assert.Equal(entry.Chat.Id, controller.List.ActiveChatId);
            Assert.True(AssistantChatStore.Exists(entry.Chat.Id));
            Assert.False(entry.Deleted);
        }
        finally
        {
            AssistantChatStore.DeleteForTest = null;
        }
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
    public async Task RestoredSessionReplaysBoundedHistoryExactlyOnce()
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
        await controller.SendAsync("third question", CodexSessionFixture.Context())
            .ConfigureAwait(false);

        Assert.Equal(2, recorded.Count);
        Assert.Contains("first question", recorded[0]);
        Assert.Contains("first answer", recorded[0]);
        Assert.Contains("second question", recorded[0]);
        Assert.DoesNotContain("tool", recorded[0], StringComparison.OrdinalIgnoreCase);

        Assert.Equal("third question", recorded[1]);
        Assert.DoesNotContain("first question", recorded[1]);
    }

    [Fact]
    public async Task NewChatReplaysItsOwnHistoryOnceThenSendsBarePrompts()
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
        await controller.SendAsync("one", CodexSessionFixture.Context()).ConfigureAwait(false);

        Assert.Single(recorded);
        Assert.Equal("one", recorded[0]);

        await controller.SendAsync("two", CodexSessionFixture.Context()).ConfigureAwait(false);
        Assert.Equal(2, recorded.Count);
        Assert.Equal("two", recorded[1]);
    }

    [Fact]
    public void OversizedPromptIsRejectedBeforeAnySessionStarts()
    {
        int factoryCalls = 0;
        Func<string, IAssistantSession?> factory = _ =>
        {
            factoryCalls++;
            return new FixedReplySession("reply");
        };
        using var controller = new AssistantConversationController(factory);
        AssistantConversationEntry entry = controller.Active!;

        AssistantReply reply = controller
            .SendAsync(new string('x', AssistantChatStore.MaxPromptCharacters + 1),
                CodexSessionFixture.Context())
            .GetAwaiter().GetResult();

        Assert.Equal("error", reply.Status);
        Assert.Equal(0, factoryCalls);
        Assert.Empty(entry.Chat.Messages);
    }

    [Fact]
    public async Task FirstUserMessageDerivesAndPersistsATitle()
    {
        using var controller = new AssistantConversationController(MakeFactory());
        AssistantConversationEntry entry = controller.Active!;
        Assert.Equal("New chat", entry.Chat.Title);

        await controller.SendAsync("How do I fix a broken clip?", CodexSessionFixture.Context())
            .ConfigureAwait(false);

        Assert.Equal("How do I fix a broken clip?", entry.Chat.Title);
        Assert.Equal(entry.Chat.Title, AssistantChatStore.Load(entry.Chat.Id)!.Title);
    }

    [Fact]
    public async Task MostRecentlyUpdatedChatSortsToTheTop()
    {
        using var controller = new AssistantConversationController(MakeFactory());
        AssistantConversationEntry a = controller.Active!;
        await Task.Delay(50);
        AssistantConversationEntry b = controller.NewChat()!;

        Assert.Equal(b.Chat.Id, controller.List.Summaries()[0].Id);

        await Task.Delay(50);
        Assert.True(controller.TrySelectById(a.Chat.Id));
        await controller.SendAsync("update a", CodexSessionFixture.Context()).ConfigureAwait(false);

        Assert.Equal(a.Chat.Id, controller.List.Summaries()[0].Id);
    }

    [Fact]
    public void RestoredToolRowsDoNotMarkTheChatAsPendingApproval()
    {
        AssistantChat chat = Build("tool-chat", "inspect it");
        chat = AssistantChatStore.AppendMessage(chat, new AssistantChatMessage(
            AssistantChatRole.Tool, "bgs.set_clip_animation (pending approval)", DateTime.UtcNow,
            ToolName: "bgs.set_clip_animation", ToolStatus: "pending approval",
            ToolRequiresApproval: true));
        AssistantChatStore.Save(chat);

        using var controller = new AssistantConversationController(MakeFactory(),
            initialChats: new[] { chat }, activeChatId: chat.Id);

        Assert.False(controller.Active!.PendingApproval);
        Assert.False(controller.Active!.ApprovalActive);
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

        controller.ApproveActive(entry);
        Assert.False(entry.ApprovalActive);

        entry = controller.NewChat()!;
        Assert.False(entry.ApprovalActive,
            "switching/NewChat must not carry approval state between conversations");
    }

    private static Func<string, IAssistantSession?> MakeFactory() =>
        _ => new FixedReplySession("hello world");

    private static AssistantChat Build(string title, string firstMessage)
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

    private sealed class TrackableSession : IAssistantSession
    {
        private readonly string _text;
        public TrackableSession(string text) => _text = text;

        public bool Disposed { get; private set; }

        public Task<AssistantReply> SendAsync(string prompt, AssistantContext context, CancellationToken cancellationToken = default)
            => Task.FromResult(new AssistantReply("ok", _text, 1, Array.Empty<AssistantToolActivity>(), false));

        public void ClearHistory() { }

        public void Dispose() => Disposed = true;
    }

    private sealed class BlockingSession : IAssistantSession
    {
        private readonly TaskCompletionSource _release =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource Started { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public bool Disposed { get; private set; }

        public async Task<AssistantReply> SendAsync(
            string prompt, AssistantContext context, CancellationToken cancellationToken = default)
        {
            Started.TrySetResult();
            await _release.Task.ConfigureAwait(false);
            return new AssistantReply("ok", "late reply", 1, Array.Empty<AssistantToolActivity>(), false);
        }

        public void Complete() => _release.TrySetResult();

        public void ClearHistory() { }

        public void Dispose() => Disposed = true;
    }
}
