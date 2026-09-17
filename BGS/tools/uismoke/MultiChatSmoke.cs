using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Threading;
using BehaviourStudio.App;
using Microsoft.Extensions.AI;

namespace BehaviourStudio.UiSmoke;

internal static class MultiChatSmoke
{
    internal static void Run()
    {
        string previousFolder = AssistantChatStore.Folder;
        string tempFolder = Path.Combine(Path.GetTempPath(),
            "bgs-multichat-smoke-" + Guid.NewGuid().ToString("N"));
        AssistantChatStore.Folder = tempFolder;
        try
        {
            var window = new MainWindow();
            window.Show();
            try
            {
                Dispatcher.UIThread.RunJobs();

                Check("assistant sidebar is visible",
                    Smoke.Find<AssistantConversationSidebar>(window).Count == 1);
                Check("the sidebar lists exactly one auto-created chat",
                    window.AssistantUiForTest.ChatListForTest.Count == 1);

                Smoke.Find<Button>(window).Single(b => b.Content?.ToString() == "Chat")
                    .RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                Dispatcher.UIThread.RunJobs();
                Check("Chat button opens the drawer to the active chat",
                    window.AssistantDrawerOpen);

                Smoke.Find<Button>(window).Single(b => b.Content?.ToString() == "+ New chat")
                    .RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                Dispatcher.UIThread.RunJobs();
                Check("+ New chat creates a second conversation in the sidebar",
                    window.AssistantUiForTest.ChatListForTest.Count == 2);

                Smoke.Find<Button>(window).Single(b => b.Content?.ToString() == "+ New chat")
                    .RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                Dispatcher.UIThread.RunJobs();
                Check("another + New chat creates a third conversation in the sidebar",
                    window.AssistantUiForTest.ChatListForTest.Count == 3);

                var chats = window.AssistantUiForTest.ChatListForTest.ToList();
                string secondId = chats[1].Id;
                string thirdId = chats[2].Id;

                OpenChatInSidebar(window, secondId);
                Dispatcher.UIThread.RunJobs();
                Check("switching to chat B leaves the active chat as B",
                    window.AssistantUiForTest.ActiveChatIdForTest == secondId);

                window.AssistantUiForTest.Pane.Composer.Text = "first message";
                var fake1 = new FakeChatClient(
                    new ChatResponse(new ChatMessage(ChatRole.Assistant, "reply-one")));
                var session = new AssistantSession(fake1, Array.Empty<AIFunction>());
                window.AssistantUiForTest.SetSessionForTest(session);
                Smoke.Find<Button>(window).Single(b => b.Content?.ToString() == "Send")
                    .RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                Dispatcher.UIThread.RunJobs();
                Check("sending in chat B updates the visible conversation",
                    window.AssistantUiForTest.MessageCount == 2 && fake1.CallCount == 1);

                OpenChatInSidebar(window, thirdId);
                Dispatcher.UIThread.RunJobs();
                Check("switching to chat C shows an empty transcript",
                    window.AssistantUiForTest.MessageCount == 0);

                OpenChatInSidebar(window, secondId);
                Dispatcher.UIThread.RunJobs();
                Check("switching back to chat B restores both messages",
                    window.AssistantUiForTest.MessageCount == 2);

                Check("created chat ids were persisted",
                    AssistantChatStore.Exists(secondId) && AssistantChatStore.Exists(thirdId));
            }
            finally
            {
                Smoke.CloseForTest(window);
            }
        }
        finally
        {
            AssistantChatStore.Folder = previousFolder;
            try { if (Directory.Exists(tempFolder)) Directory.Delete(tempFolder, true); }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException) { }
        }
    }

    private static void OpenChatInSidebar(MainWindow window, string chatId)
    {
        window.AssistantUiForTest.SelectChatForTest(chatId);
    }

    private static void Check(string name, bool value) => Smoke.CheckTrue(name, value);

    private sealed class FakeChatClient : IChatClient
    {
        private readonly Queue<ChatResponse> _responses;
        public FakeChatClient(params ChatResponse[] responses) => _responses = new(responses);
        public int CallCount { get; private set; }
        public Task<ChatResponse> GetResponseAsync(IEnumerable<ChatMessage> messages,
            ChatOptions? options = null, CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            CallCount++;
            return Task.FromResult(_responses.Dequeue());
        }
        public async IAsyncEnumerable<ChatResponseUpdate> GetStreamingResponseAsync(
            IEnumerable<ChatMessage> messages, ChatOptions? options = null,
            [System.Runtime.CompilerServices.EnumeratorCancellation]
            CancellationToken cancellationToken = default)
        {
            yield break;
        }
        public object? GetService(Type serviceType, object? serviceKey = null) => null;
        public void Dispose() { }
    }
}
