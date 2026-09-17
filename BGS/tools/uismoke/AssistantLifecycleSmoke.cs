using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Threading;
using BehaviourStudio.App;
using Microsoft.Extensions.AI;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.UiSmoke;

internal static class AssistantLifecycleSmoke
{
    internal static void Run()
    {
        string previousFolder = AssistantChatStore.Folder;
        string tempFolder = Path.Combine(Path.GetTempPath(),
            "bgs-assistant-smoke-" + Guid.NewGuid().ToString("N"));
        AssistantChatStore.Folder = tempFolder;
        try
        {
            RunInternal();
        }
        finally
        {
            AssistantChatStore.Folder = previousFolder;
            try { if (Directory.Exists(tempFolder)) Directory.Delete(tempFolder, true); }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException) { }
        }
    }

    private static void RunInternal()
    {
        var window = new MainWindow();
        window.Show();
        try
        {
            Dispatcher.UIThread.RunJobs();
            Check("assistant starts closed", !window.AssistantDrawerOpen);
            Check("Chat entry point exists once", Smoke.Find<Button>(window)
                .Count(button => button.Content?.ToString() == "Chat") == 1);
            Check("Assistant settings entry point exists once", Smoke.Find<Button>(window)
                .Count(button => button.Content?.ToString() == "Assistant settings") == 1);
            Check("assistant advertises the ChatGPT subscription boundary without a request",
                window.AssistantUiForTest.ProviderStatus.Contains("ChatGPT via Codex", StringComparison.Ordinal));
            Check("assistant starts signed out and offers sign in",
                window.AssistantUiForTest.AccountStatus == "Signed out" && window.AssistantUiForTest.SignInVisible);

            string activity = window.SelectedActivity;
            var fake = new FakeChatClient(
                new ChatResponse(new ChatMessage(ChatRole.Assistant, "Hello from the test provider.")));
            var settings = Smoke.Find<Button>(window)
                .Single(button => button.Content?.ToString() == "Assistant settings");
            settings.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Dispatcher.UIThread.RunJobs();
            Check("Assistant settings opens the drawer", window.AssistantDrawerOpen);
            Check("Assistant settings preserves active activity", activity == window.SelectedActivity);
            Check("Assistant settings exposes ChatGPT sign in",
                window.AssistantUiForTest.SignInVisible &&
                window.AssistantUiForTest.Status.Contains("Sign in with ChatGPT", StringComparison.Ordinal));
            Check("opening Assistant settings makes no provider request", fake.CallCount == 0);

            var chat = Smoke.Find<Button>(window).Single(button => button.Content?.ToString() == "Chat");
            chat.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Dispatcher.UIThread.RunJobs();
            Check("Chat opens the assistant drawer", window.AssistantDrawerOpen);
            Check("Chat preserves active activity", activity == window.SelectedActivity);
            Check("opening Chat makes no provider request", fake.CallCount == 0);
            Check("opening Chat leaves the account signed out",
                window.AssistantUiForTest.AccountStatus == "Signed out" &&
                window.AssistantUiForTest.SignInVisible);

            AssistantTools smokeTools = window.CreateAssistantTools();
            window.AssistantUiForTest.SetSessionForTest(
                window.CreateAssistantSession(fake, smokeTools), smokeTools);
            window.AssistantUiForTest.Pane.Composer.Text = "Say hello.";
            Smoke.Find<Button>(window).Single(button => button.Content?.ToString() == "Send")
                .RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Dispatcher.UIThread.RunJobs();
            Check("send reaches the configured provider", fake.CallCount == 1);
            Check("conversation shows the user and assistant messages",
                window.AssistantUiForTest.MessageCount == 2);
            Check("completed request returns to ready", window.AssistantUiForTest.Status == "Ready.");

            Smoke.Find<Button>(window).Single(button => button.Content?.ToString() == "Close")
                .RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Dispatcher.UIThread.RunJobs();
            Check("closing assistant restores the shell", !window.AssistantDrawerOpen);
        }
        finally
        {
            Smoke.CloseForTest(window);
        }

        WindowOwnsCodexConnection();
        ActiveEditorApproval();
    }

    private static void WindowOwnsCodexConnection()
    {
        var window = new MainWindow();
        var transport = new WindowTransport();
        window.Show();
        try
        {
            string executable = Environment.ProcessPath
                ?? throw new InvalidOperationException("the smoke process path is unavailable");
            var options = new AssistantProviderOptions(
                AssistantProviderOptions.CodexBackend, "", executable);
            CodexAssistantConnection connection = window.CreateCodexConnection(options, (_, _) => transport);
            connection.ConnectAsync().GetAwaiter().GetResult();
            Check("window-owned Codex connection starts", connection.IsConnected && transport.IsRunning);
        }
        finally
        {
            Smoke.CloseForTest(window);
        }
        Check("closing window disposes the Codex connection", transport.Disposed && !transport.IsRunning);
    }

    private static void ActiveEditorApproval()
    {
        var window = new MainWindow();
        window.Show();
        try
        {
            string path = Path.Combine("tools", "tests", "fixtures", "vanilla", "Meshes", "Actors",
                "Character", "Behaviors", "SingleAnimFurniture.hkx");
            window.Open(path);
            Dispatcher.UIThread.RunJobs();
            string id = BehaviourGraphModel.Parse(window.LoadedXml).Objects
                .First(objectInfo => objectInfo.Class == "hkbClipGenerator").Id;
            var fake = new FakeChatClient(
                new ChatResponse(new ChatMessage(ChatRole.Assistant, new List<AIContent>
                {
                    new FunctionCallContent("call-1", "bgs.set_clip_animation",
                        new Dictionary<string, object?>
                        {
                            ["objectId"] = id,
                            ["animationName"] = "Animations\\Assistant\\Approved.hkx",
                        }),
                })),
                new ChatResponse(new ChatMessage(ChatRole.Assistant,
                    "The preview is ready for your approval.")));
            AssistantTools approvalTools = window.CreateAssistantTools();
            window.AssistantUiForTest.SetSessionForTest(
                window.CreateAssistantSession(fake, approvalTools), approvalTools);
            window.AssistantUiForTest.Pane.Composer.Text = "Update the selected clip.";
            Smoke.Find<Button>(window).Single(button => button.Content?.ToString() == "Send")
                .RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Dispatcher.UIThread.RunJobs();
            Check("model can invoke the real clip preview tool", fake.CallCount == 2);
            Check("preview waits for explicit approval",
                window.AssistantUiForTest.ApprovalVisible && !window.IsDirty);

            Smoke.Find<Button>(window).Single(button => button.Content?.ToString() == "Apply approved edit")
                .RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Dispatcher.UIThread.RunJobs();
            Check("approval uses the normal editor commit", window.IsDirty && window.UndoStepsForTest == 1);
            Check("approved animation reaches the active document",
                window.LoadedXml.Contains("Approved.hkx", StringComparison.Ordinal));
        }
        finally
        {
            Smoke.CloseForTest(window);
        }
    }

    private static void Check(string name, bool value) => Smoke.CheckTrue(name, value);

    private sealed class WindowTransport : ICodexTransport
    {
        public event Action<string>? LineReceived;
        public event Action<CodexProcessExit>? Exited;

        public bool IsRunning { get; private set; }
        internal bool Disposed { get; private set; }

        public Task StartAsync(CancellationToken cancellationToken)
        {
            IsRunning = true;
            return Task.CompletedTask;
        }

        public Task SendLineAsync(string line, CancellationToken cancellationToken)
        {
            using JsonDocument document = JsonDocument.Parse(line);
            JsonElement root = document.RootElement;
            if (!root.TryGetProperty("id", out JsonElement id) ||
                !root.TryGetProperty("method", out JsonElement methodElement))
                return Task.CompletedTask;

            string method = methodElement.GetString() ?? "";
            string result = method switch
            {
                "initialize" => "{\"userAgent\":\"behaviour_graph_studio/0.154.0\",\"platformFamily\":\"windows\",\"platformOs\":\"windows\"}",
                "account/read" => "{\"account\":{\"type\":\"chatgpt\",\"email\":null,\"planType\":\"plus\"},\"requiresOpenaiAuth\":true}",
                "model/list" => "{\"data\":[]}",
                _ => "{}",
            };
            LineReceived?.Invoke("{\"id\":" + id.GetRawText() + ",\"result\":" + result + "}");
            return Task.CompletedTask;
        }

        public void Dispose()
        {
            Disposed = true;
            IsRunning = false;
        }
    }

    private sealed class FakeChatClient : IChatClient
    {
        private readonly Queue<ChatResponse> _responses;

        internal FakeChatClient(params ChatResponse[] responses) => _responses = new(responses);
        internal int CallCount { get; private set; }

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