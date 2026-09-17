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
        string? previousSettings = Settings.SettingsPathForTest;
        string tempFolder = Path.Combine(Path.GetTempPath(),
            "bgs-assistant-smoke-" + Guid.NewGuid().ToString("N"));
        string settingsPath = Path.Combine(Path.GetTempPath(),
            "bgs-assistant-settings-" + Guid.NewGuid().ToString("N") + ".cfg");
        AssistantChatStore.Folder = tempFolder;
        Settings.SettingsPathForTest = settingsPath;
        try
        {
            RunInternal();
        }
        finally
        {
            AssistantChatStore.Folder = previousFolder;
            Settings.SettingsPathForTest = previousSettings;
            try { if (Directory.Exists(tempFolder)) Directory.Delete(tempFolder, true); }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException) { }
            try { if (File.Exists(settingsPath)) File.Delete(settingsPath); }
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
            EditorShell? shell = EditorShell.Find(window.Content as Control);
            Check("the left rail owns both assistant entry points",
                shell is { ChatButton: not null, SettingsButton: not null });
            Check("the rail icons carry the chat and settings glyphs",
                shell!.ChatButton.Content?.ToString() == EditorShell.ChatGlyph &&
                shell.SettingsButton.Content?.ToString() == EditorShell.SettingsGlyph);
            Check("the toolbar ChatGPT box is gone",
                !Smoke.Find<Button>(window).Any(button => button.Content?.ToString() == "Chat") &&
                !Smoke.Find<Button>(window).Any(button => button.Content?.ToString() == "Assistant settings"));
            Check("assistant advertises the ChatGPT subscription boundary without a request",
                window.AssistantUiForTest.ProviderStatus.Contains("ChatGPT via Codex", StringComparison.Ordinal));
            Check("assistant starts signed out and offers sign in",
                window.AssistantUiForTest.AccountStatus == "Signed out" && window.AssistantUiForTest.SignInVisible);

            string activity = window.SelectedActivity;
            var fake = new FakeChatClient(
                new ChatResponse(new ChatMessage(ChatRole.Assistant, "Hello from the test provider.")));
            shell.SettingsButton.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Dispatcher.UIThread.RunJobs();
            Check("Assistant settings opens its own window, not the chat drawer",
                window.AssistantUiForTest.SettingsWindowForTest is { IsVisible: true } && !window.AssistantDrawerOpen);
            Check("Assistant settings preserves active activity", activity == window.SelectedActivity);
            Check("Assistant settings exposes ChatGPT sign in",
                window.AssistantUiForTest.SettingsWindowForTest!.SignInVisible);
            Check("opening Assistant settings makes no provider request", fake.CallCount == 0);

            shell.ChatButton.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
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
        TheGearOpensAssistantSettings();
        ModelSelectorListsCatalogAndPersistsChoice();
        ProviderSelectionIsExplicitAndPersisted();
        ActiveEditorApproval();
        StaleApprovalIsNotReportedAsApplied();
    }

    private static void StaleApprovalIsNotReportedAsApplied()
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
            AssistantTools tools = window.CreateAssistantTools();
            window.AssistantUiForTest.SetSessionForTest(
                window.CreateAssistantSession(fake, tools), tools);
            window.AssistantUiForTest.Pane.Composer.Text = "Update the selected clip.";
            Smoke.Find<Button>(window).Single(button => button.Content?.ToString() == "Send")
                .RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Dispatcher.UIThread.RunJobs();
            Check("stale scenario starts with a pending preview",
                window.AssistantUiForTest.ApprovalVisible);

            string advanced = HkxTextEdit.SetParam(
                window.LoadedXml, id, "animationName", "Animations\\Assistant\\Other.hkx");
            bool committed = ((IAssistantEditorDocument)window).TryCommit(advanced, out _);
            Check("the active document advanced before approval", committed);

            Smoke.Find<Button>(window).Single(button => button.Content?.ToString() == "Apply approved edit")
                .RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Dispatcher.UIThread.RunJobs();
            Check("a stale approval is reported as not applied",
                window.AssistantUiForTest.Status.Contains("was not applied", StringComparison.Ordinal));
            Check("a stale approval never reaches the document",
                !window.LoadedXml.Contains("Approved.hkx", StringComparison.Ordinal));
        }
        finally
        {
            Smoke.CloseForTest(window);
        }
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

    private static void ModelSelectorListsCatalogAndPersistsChoice()
    {
        Settings.TrySet("assistant.model", "", out _);
        var window = new MainWindow();
        var transport = new WindowTransport();
        window.Show();
        try
        {
            string executable = Environment.ProcessPath
                ?? throw new InvalidOperationException("the smoke process path is unavailable");
            var options = new AssistantProviderOptions(
                AssistantProviderOptions.CodexBackend, "", executable);
            CodexAssistantConnection connection =
                window.CreateCodexConnection(options, (_, _) => transport);
            connection.ConnectAsync().GetAwaiter().GetResult();
            window.AssistantUiForTest.SetConnectionForTest(connection);
            window.AssistantUiForTest.RefreshAccountForTest();

            AssistantPane pane = window.AssistantUiForTest.Pane;
            Check("the model selector appears once the catalog is known", pane.ModelSelectorVisible);
            Check("the model selector lists every catalog model", pane.ModelSelector.ItemCount == 3);
            Check("no model is forced before the user chooses", pane.SelectedModelItem is null);
            Check("the account line names the Codex config default",
                window.AssistantUiForTest.AccountStatus.Contains("Codex config default", StringComparison.Ordinal));

            AssistantModelItem astra = pane.ModelSelector.ItemsSource!.Cast<AssistantModelItem>()
                .Single(item => item.WireModel == "gpt-6-astra");
            pane.ModelSelector.SelectedItem = astra;
            Dispatcher.UIThread.RunJobs();
            Check("choosing a model persists it", Settings.Get("assistant.model") == "gpt-6-astra");
            Check("choosing a model selects it on the connection", connection.SelectedModel == "gpt-6-astra");
            Check("choosing a model shows the chosen model", pane.SelectedModelItem == "gpt-6-astra");
            Check("choosing a model offers a reset", pane.ModelResetVisible);

            pane.InvokeModelResetRequested();
            Dispatcher.UIThread.RunJobs();
            Check("resetting clears the choice", Settings.Get("assistant.model") == "");
            Check("resetting returns to the Codex config default", connection.SelectedModel == "");
            Check("resetting hides the reset control", !pane.ModelResetVisible);
        }
        finally
        {
            Smoke.CloseForTest(window);
        }
    }

    internal static void OpenChat(MainWindow window)
    {
        EditorShell shell = EditorShell.Find(window.Content as Control)
            ?? throw new InvalidOperationException("the editor shell is unavailable");
        shell.ChatButton.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        Dispatcher.UIThread.RunJobs();
    }

    private static void TheGearOpensAssistantSettings()
    {
        var window = new MainWindow();
        window.Show();
        try
        {
            Dispatcher.UIThread.RunJobs();
            EditorShell? shell = EditorShell.Find(window.Content as Control);
            Check("the shell hosts the chat and settings icons",
                shell?.ChatButton != null && shell.SettingsButton != null);
            Check("the settings gear uses the settings glyph",
                shell!.SettingsButton.Content?.ToString() == EditorShell.SettingsGlyph);
            Check("the assistant starts closed", !window.AssistantDrawerOpen);
            Check("no settings window is open at startup", window.AssistantUiForTest.SettingsWindowForTest is null);

            shell.SettingsButton.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Dispatcher.UIThread.RunJobs();
            AssistantSettingsWindow? settings = window.AssistantUiForTest.SettingsWindowForTest;
            Check("the gear opens a dedicated settings window", settings is { IsVisible: true });
            Check("opening settings does not open the chat drawer", !window.AssistantDrawerOpen);
            Check("settings offers the provider", settings!.ProviderSelector.ItemCount == 3);
            Check("settings offers the model control", settings.ModelSelector != null);
            Check("Codex settings expose ChatGPT sign in", settings.SignInVisible);
            Check("settings explains the ChatGPT boundary",
                settings.NoticeText.Contains("ChatGPT sign-in", StringComparison.Ordinal));

            settings.CloseForTest();
            Dispatcher.UIThread.RunJobs();
            Check("the settings window closes", !settings.IsVisible);

            shell.ChatButton.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Dispatcher.UIThread.RunJobs();
            Check("the chat icon above the gear opens the chat drawer", window.AssistantDrawerOpen);
            Check("the chat icon does not reopen settings", !settings.IsVisible);
        }
        finally
        {
            Smoke.CloseForTest(window);
        }
    }

    private static void ProviderSelectionIsExplicitAndPersisted()
    {
        Settings.TrySet("assistant.backend", "", out _);
        Func<string> previousRoot = ClaudeModelCatalog.ConfigRootForTest;
        Func<string, IReadOnlyList<string>> previousFiles = ClaudeModelCatalog.FilesForTest;
        Func<string, string?> previousRead = ClaudeModelCatalog.ReadFileForTest;
        ClaudeModelCatalog.FilesForTest = _ => new[] { "catalog.json" };
        ClaudeModelCatalog.ReadFileForTest = _ =>
            "{\"catalog\":{\"config\":{\"models\":[" +
            "{\"id\":\"claude-opus-5\",\"name\":\"Opus 5\"}," +
            "{\"id\":\"claude-haiku-4-5-20251001\",\"name\":\"Haiku 4.5\"}]}}}";

        var window = new MainWindow();
        window.Show();
        try
        {
            AssistantPane pane = window.AssistantUiForTest.Pane;
            Check("the provider selector is present", pane.ProviderSelectorVisible);
            string[] offered = pane.ProviderSelector.ItemsSource!.Cast<AssistantProviderItem>()
                .Select(item => item.Wire).ToArray();
            Check("every provider is offered exactly once",
                offered.SequenceEqual(new[] { "codex", "claude", "opencode" }));
            Check("the assistant starts on Codex", pane.SelectedProvider == "codex");
            Check("Codex keeps the ChatGPT sign-in", pane.SignInVisible);

            pane.InvokeProviderSelected("claude");
            Dispatcher.UIThread.RunJobs();
            Check("choosing a provider persists it", Settings.Get("assistant.backend") == "claude");
            Check("choosing a provider is reported",
                window.AssistantUiForTest.Status.Contains("Claude Code", StringComparison.Ordinal));
            Check("the provider description follows the choice",
                window.AssistantUiForTest.ProviderStatus.Contains("Claude Code", StringComparison.Ordinal));
            Check("Claude uses its own sign-in rather than BGS",
                window.AssistantUiForTest.AccountStatus.Contains("claude", StringComparison.OrdinalIgnoreCase));
            Check("the ChatGPT sign-in buttons are withdrawn for a CLI provider", !pane.SignInVisible);
            Check("the notice explains the CLI-managed sign-in",
                pane.NoticeText.Contains("own command-line tool", StringComparison.Ordinal));

            Check("Claude offers a real model list rather than free text",
                pane.ModelSelector.ItemCount == 2);
            string[] claudeModels = pane.ModelSelector.ItemsSource!
                .Cast<AssistantModelItem>().Select(item => item.WireModel).ToArray();
            Check("the Claude list is the catalog's list",
                claudeModels.SequenceEqual(new[] { "claude-opus-5", "claude-haiku-4-5-20251001" }));
            Check("an unset Claude model names the CLI default",
                pane.ModelSelector.PlaceholderText == "Claude CLI default");

            pane.ModelSelector.SelectedItem = pane.ModelSelector.ItemsSource!
                .Cast<AssistantModelItem>().Single(item => item.WireModel == "claude-opus-5");
            Dispatcher.UIThread.RunJobs();
            Check("a chosen Claude model is persisted",
                Settings.Get(AssistantUi.ModelSettingKey(AssistantBackend.Claude)) == "claude-opus-5");
            Check("each provider keeps its own model",
                AssistantUi.ModelSettingKey(AssistantBackend.Claude) !=
                AssistantUi.ModelSettingKey(AssistantBackend.Codex));
        }
        finally
        {
            Smoke.CloseForTest(window);
            ClaudeModelCatalog.ConfigRootForTest = previousRoot;
            ClaudeModelCatalog.FilesForTest = previousFiles;
            ClaudeModelCatalog.ReadFileForTest = previousRead;
        }

        var restored = new MainWindow();
        restored.Show();
        try
        {
            Check("the chosen provider is restored on the next window",
                restored.AssistantUiForTest.Pane.SelectedProvider == "claude");
        }
        finally
        {
            Smoke.CloseForTest(restored);
            Settings.TrySet("assistant.backend", "", out _);
            Settings.TrySet(AssistantUi.ModelSettingKey(AssistantBackend.Claude), "", out _);
        }
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
        internal const string Catalog =
            "{\"data\":[" +
            "{\"id\":\"gpt-5.6-sol\",\"model\":\"gpt-5.6-sol\",\"displayName\":\"GPT-5.6-Sol\",\"isDefault\":true}," +
            "{\"id\":\"gpt-6-astra\",\"model\":\"gpt-6-astra\",\"displayName\":\"GPT-6-Astra\"}," +
            "{\"id\":\"gpt-5.5\",\"model\":\"gpt-5.5\",\"displayName\":\"GPT-5.5\"}" +
            "],\"nextCursor\":null}";

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
                "model/list" => WindowTransport.Catalog,
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