using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Documents;
using Avalonia.Headless;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Threading;
using Avalonia.VisualTree;
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
        Func<Task<IReadOnlyList<CodexModel>>>? previousCatalog = AssistantUi.AllAgentsForTest;
        AssistantUi.AllAgentsForTest = () => Task.FromResult(SmokeCatalog());
        try
        {
            RunInternal();
        }
        finally
        {
            AssistantUi.AllAgentsForTest = previousCatalog;
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

        QuickStartShowsOnceOnFirstOpen();
        WindowOwnsCodexConnection();
        TheGearOpensAssistantSettings();
        AssistantRepliesRenderAsMarkdown();
        ModelSelectorListsCatalogAndPersistsChoice();
        ThePaletteFiltersByAgentAndSwitchesProvider();
        ApiProvidersUseASessionKeyAndQueryTheEndpoint();
        ProgressShowsWhileATurnRuns();
        ComposerSendsQueuesAndRunsCommands();
        SlashCommandsCompleteInTheComposer();
        ProviderSelectionIsExplicitAndPersisted();
        ActiveEditorApproval();
        StaleApprovalIsNotReportedAsApplied();
    }

    private static IReadOnlyList<CodexModel> SmokeCatalog()
    {
        var models = new List<CodexModel>
        {
            new("gpt-5.6-sol", "GPT-5.6-Sol", true, "gpt-5.6-sol") { Agent = "Codex", Group = "Codex" },
            new("gpt-6-astra", "GPT-6-Astra", false, "gpt-6-astra") { Agent = "Codex", Group = "Codex" },
            new("gpt-5.5", "GPT-5.5", false, "gpt-5.5") { Agent = "Codex", Group = "Codex" },
            new("claude-opus-5", "Opus 5", false, "claude-opus-5") { Agent = "Claude", Group = "Claude" },
            new("claude-haiku-4-5-20251001", "Haiku 4.5", false, "claude-haiku-4-5-20251001")
            {
                Agent = "Claude", Group = "Claude",
            },
        };
        for (int index = 0; index < 12; index++)
        {
            string wire = "opencode-go/model-" + index.ToString("00");
            models.Add(new CodexModel(wire, "opencode model " + index.ToString("00"), false, wire)
            {
                Agent = "opencode",
                Group = "opencode-go",
                ContextLimit = index == 0 ? 1_000_000 : 200_000,
                Reasoning = index % 2 == 0,
            });
        }
        return models.AsReadOnly();
    }

    private static void ThePaletteFiltersByAgentAndSwitchesProvider()
    {
        Settings.TrySet("assistant.backend", "", out _);
        Settings.TrySet(AssistantUi.ModelSettingKey(AssistantBackend.Codex), "", out _);
        Settings.TrySet(AssistantUi.ModelSettingKey(AssistantBackend.Claude), "", out _);
        Settings.TrySet(AssistantUi.ModelSettingKey(AssistantBackend.Opencode), "", out _);
        var window = new MainWindow();
        window.Show();
        try
        {
            OpenChat(window);
            AssistantPane pane = window.AssistantUiForTest.Pane;
            pane.ModelButton.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Dispatcher.UIThread.RunJobs();
            Check("the model control opens an in-pane palette", pane.ModelPanelOpen);

            AssistantModelPicker picker = pane.ModelPicker;
            Check("the palette lists every agent at once", picker.VisibleModels.Count == 17);
            Check("the palette offers a filter for every agent",
                picker.FilterLabels.SequenceEqual(new[] { "All", "Codex", "Claude", "opencode" }));

            Point? listPoint = picker.ModelList.TranslatePoint(new Point(14, 14), window);
            Check("the palette list is reachable for pointer input", listPoint is not null);
            double offsetBefore = picker.ModelList.Offset.Y;
            window.MouseWheel(listPoint!.Value, new Vector(0, -120));
            Dispatcher.UIThread.RunJobs();
            Check("the mouse wheel scrolls the palette", picker.ModelList.Offset.Y > offsetBefore);

            picker.SetAgentFilter("Claude");
            Check("the agent filter narrows the palette to that agent",
                picker.VisibleModels.SequenceEqual(new[] { "claude-opus-5", "claude-haiku-4-5-20251001" }));
            picker.SetQuery("haiku");
            Check("search and the agent filter combine",
                picker.VisibleModels.SequenceEqual(new[] { "claude-haiku-4-5-20251001" }));
            picker.SetQuery("");
            picker.SetAgentFilter("");
            Check("clearing the filter restores every agent", picker.VisibleModels.Count == 17);

            picker.FocusSearch();
            Dispatcher.UIThread.RunJobs();
            window.KeyPress(Key.Escape, RawInputModifiers.None, PhysicalKey.Escape, null);
            Dispatcher.UIThread.RunJobs();
            Check("Escape dismisses the palette", !pane.ModelPanelOpen);

            pane.ModelButton.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Dispatcher.UIThread.RunJobs();
            picker.SetAgentFilter("Claude");
            Dispatcher.UIThread.RunJobs();
            Border? row = picker.GetVisualDescendants().OfType<Border>()
                .FirstOrDefault(border => border.Tag is int index && index == 0);
            Check("the palette exposes clickable rows", row is not null);
            Point? rowPoint = row?.TranslatePoint(new Point(6, 6), window);
            Check("the row is reachable for a mouse click", rowPoint is not null);
            window.MouseDown(rowPoint!.Value, MouseButton.Left);
            window.MouseUp(rowPoint.Value, MouseButton.Left);
            Dispatcher.UIThread.RunJobs();
            Check("clicking a row chooses that model",
                Settings.Get(AssistantUi.ModelSettingKey(AssistantBackend.Claude)) == "claude-opus-5");
            Check("clicking another agent's row switches provider",
                window.AssistantUiForTest.Backend == AssistantBackend.Claude);
            Check("choosing closes the palette", !pane.ModelPanelOpen);
            Check("the model button names the clicked model",
                pane.ModelButtonLabel.Contains("Opus 5", StringComparison.Ordinal));
        }
        finally
        {
            Smoke.CloseForTest(window);
            Settings.TrySet("assistant.backend", "", out _);
            Settings.TrySet(AssistantUi.ModelSettingKey(AssistantBackend.Claude), "", out _);
        }
    }

    private static void ApiProvidersUseASessionKeyAndQueryTheEndpoint()
    {
        Settings.TrySet("assistant.backend", "", out _);
        Settings.TrySet(ApiProviders.BaseUrlSetting(AssistantBackend.Local), "", out _);
        Settings.TrySet(AssistantUi.ModelSettingKey(AssistantBackend.Local), "", out _);
        Func<string, string, CancellationToken, Task<ApiModelList>> previousFetch =
            ApiModelCatalog.FetchForTest;
        Func<Task<IReadOnlyList<CodexModel>>>? previousCatalog = AssistantUi.AllAgentsForTest;
        string? fetchedUrl = null;
        ApiModelCatalog.FetchForTest = (baseUrl, _, _) =>
        {
            fetchedUrl = baseUrl;
            return Task.FromResult(new ApiModelList(new[] { "local-a", "local-b" }, ""));
        };
        AssistantUi.AllAgentsForTest = () => Task.FromResult<IReadOnlyList<CodexModel>>(
            SmokeCatalog().Concat(
                AssistantUi.ApiModels(AssistantBackend.Local, new[] { "local-a", "local-b" })).ToList());
        var window = new MainWindow();
        window.Show();
        try
        {
            AssistantPane pane = window.AssistantUiForTest.Pane;
            window.AssistantUiForTest.OpenSettings();
            Dispatcher.UIThread.RunJobs();
            AssistantSettingsWindow settings = window.AssistantUiForTest.SettingsWindowForTest!;
            Check("a CLI provider hides the API connection section", !settings.ApiSectionVisible);

            settings.ProviderSelector.SelectedItem = settings.ProviderSelector.ItemsSource!
                .Cast<AssistantProviderItem>().Single(item => item.Wire == "local");
            Dispatcher.UIThread.RunJobs();
            Check("choosing Local switches to the API provider",
                window.AssistantUiForTest.Backend == AssistantBackend.Local);
            Check("an API provider shows the connection section", settings.ApiSectionVisible);
            Check("Local defaults to the Ollama endpoint",
                settings.ApiBaseForTest.Text.Contains("11434", StringComparison.Ordinal));
            Check("choosing Local queries its endpoint for models",
                fetchedUrl is not null && fetchedUrl.Contains("11434", StringComparison.Ordinal));
            pane.ModelPicker.SetAgentFilter("Local");
            Check("the endpoint's models appear under the Local agent",
                pane.ModelPicker.VisibleModels.SequenceEqual(new[] { "local-a", "local-b" }));
            pane.ModelPicker.SetAgentFilter("");
            Check("the notice says BGS ships and stores no key",
                settings.NoticeText.Contains("never writes one to disk", StringComparison.Ordinal));

            Check("the API key box is masked", settings.ApiKeyForTest.PasswordChar == '\u2022');
            settings.ApiKeyForTest.Text = "sk-test-secret";
            settings.ApiKeyForTest.RaiseEvent(new KeyEventArgs
            {
                RoutedEvent = InputElement.KeyDownEvent,
                Key = Key.Enter,
            });
            Dispatcher.UIThread.RunJobs();
            Check("the key is held for this session",
                window.AssistantUiForTest.ApiKeyForTest == "sk-test-secret");

            string store = Settings.SettingsPathForTest is { Length: > 0 } path && File.Exists(path)
                ? File.ReadAllText(path)
                : "";
            Check("the key is never written to the settings file",
                !store.Contains("sk-test-secret", StringComparison.Ordinal));

            Smoke.Find<Button>(settings).Single(button => button.Content?.ToString() == "Sign out")
                .RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Dispatcher.UIThread.RunJobs();
            Check("signing out clears the session key",
                window.AssistantUiForTest.ApiKeyForTest.Length == 0);
        }
        finally
        {
            Smoke.CloseForTest(window);
            ApiModelCatalog.FetchForTest = previousFetch;
            AssistantUi.AllAgentsForTest = previousCatalog;
            Settings.TrySet("assistant.backend", "", out _);
            Settings.TrySet(ApiProviders.BaseUrlSetting(AssistantBackend.Local), "", out _);
            Settings.TrySet(AssistantUi.ModelSettingKey(AssistantBackend.Local), "", out _);
        }
    }

    private static void ProgressShowsWhileATurnRuns()
    {
        var window = new MainWindow();
        window.Show();
        try
        {
            OpenChat(window);
            AssistantPane pane = window.AssistantUiForTest.Pane;
            var modelGate = new TaskCompletionSource<ChatResponse>();
            var toolGate = new TaskCompletionSource<string>();
            var tool = AIFunctionFactory.Create(() => toolGate.Task, "bgs.inspect_behavior", "blocks");
            var client = new GatedChatClient(modelGate,
                new ChatResponse(new ChatMessage(ChatRole.Assistant, "All done.")));
            AssistantTools tools = window.CreateAssistantTools();
            window.AssistantUiForTest.SetSessionForTest(new AssistantSession(client, new[] { tool }), tools);

            pane.Composer.Text = "Look at this.";
            Smoke.Find<Button>(window).Single(button => button.Content?.ToString() == "Send")
                .RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Dispatcher.UIThread.RunJobs();

            Check("a working row shows while the turn runs",
                pane.ProgressVisible && pane.ProgressText.Length > 0);

            modelGate.SetResult(new ChatResponse(new ChatMessage(ChatRole.Assistant, new List<AIContent>
            {
                new FunctionCallContent("call-1", "bgs.inspect_behavior", new Dictionary<string, object?>()),
            })));
            Pump(() => pane.ProgressText.Contains("bgs.inspect_behavior", StringComparison.Ordinal));

            Check("the running tool is named in the working row",
                pane.ProgressText.Contains("bgs.inspect_behavior", StringComparison.Ordinal));
            Check("the tool call appears in the conversation while it runs",
                Smoke.Find<TextBlock>(window).Any(text =>
                    (text.Text ?? "").Contains("bgs.inspect_behavior: running", StringComparison.Ordinal)));

            toolGate.SetResult("ok");
            Pump(() => !pane.ProgressVisible);

            Check("the working row clears when the turn ends", !pane.ProgressVisible);

            static string Shown(TextBlock block) => block.Text is { Length: > 0 } value
                ? value
                : block.Inlines is null
                    ? ""
                    : string.Concat(block.Inlines.OfType<Run>().Select(run => run.Text));
            Check("the turn's answer reaches the conversation",
                Smoke.Find<TextBlock>(window).Any(block =>
                    Shown(block).Contains("All done.", StringComparison.Ordinal)));
        }
        finally
        {
            Smoke.CloseForTest(window);
        }
    }

    private static void Pump(Func<bool> until, int milliseconds = 3000)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(milliseconds);
        while (DateTime.UtcNow < deadline)
        {
            Dispatcher.UIThread.RunJobs();
            if (until()) return;
            Thread.Sleep(10);
        }
        Dispatcher.UIThread.RunJobs();
    }

    private static void ComposerSendsQueuesAndRunsCommands()
    {
        var window = new MainWindow();
        window.Show();
        try
        {
            OpenChat(window);
            AssistantPane pane = window.AssistantUiForTest.Pane;
            var gate = new TaskCompletionSource<ChatResponse>();
            var client = new GatedChatClient(gate,
                new ChatResponse(new ChatMessage(ChatRole.Assistant, "Second answer.")));
            AssistantTools tools = window.CreateAssistantTools();
            window.AssistantUiForTest.SetSessionForTest(
                new AssistantSession(client, Array.Empty<AIFunction>()), tools);

            pane.Composer.Text = "first message";
            PressEnter(window, pane, shift: false);
            Check("Enter sends the composer text", pane.Composer.Text == "" && pane.IsBusy);
            Check("the sent message appears in the conversation",
                TextShown(window).Contains("first message", StringComparison.Ordinal));

            pane.Composer.Text = "draft";
            PressEnter(window, pane, shift: true);
            Check("Shift+Enter keeps the draft instead of sending",
                (pane.Composer.Text ?? "").Contains("draft", StringComparison.Ordinal) &&
                !TextShown(window).Contains("Queued: draft", StringComparison.Ordinal));

            pane.Composer.Text = "second message";
            PressEnter(window, pane, shift: false);
            Check("a message sent during a turn is queued",
                TextShown(window).Contains("Queued: second message", StringComparison.Ordinal));

            gate.SetResult(new ChatResponse(new ChatMessage(ChatRole.Assistant, "First answer.")));
            Pump(() => !pane.IsBusy && !TextShown(window).Contains("Queued:", StringComparison.Ordinal));
            Check("the queued message sends when the turn ends",
                TextShown(window).Contains("Second answer.", StringComparison.Ordinal));
            Check("the queue row clears once it sends",
                !TextShown(window).Contains("Queued:", StringComparison.Ordinal));

            pane.Composer.Text = "/help";
            PressEnter(window, pane, shift: false);
            Check("/help lists the BGS commands",
                TextShown(window).Contains("/new: Start a new chat", StringComparison.Ordinal));

            pane.Composer.Text = "/clear";
            PressEnter(window, pane, shift: false);
            Check("/clear empties the conversation", pane.MessageCount == 0);

            int chats = window.AssistantUiForTest.ChatListForTest.Count;
            pane.Composer.Text = "/new";
            PressEnter(window, pane, shift: false);
            Check("/new starts a new chat",
                window.AssistantUiForTest.ChatListForTest.Count > chats);
        }
        finally
        {
            Smoke.CloseForTest(window);
        }
    }

    private static void SlashCommandsCompleteInTheComposer()
    {
        var window = new MainWindow();
        window.Show();
        try
        {
            OpenChat(window);
            AssistantPane pane = window.AssistantUiForTest.Pane;

            pane.Composer.Text = "/";
            Dispatcher.UIThread.RunJobs();
            Check("typing a slash shows the command list", pane.CommandsVisible);
            Check("the list holds the BGS commands",
                pane.CommandChoices.Any(command => command.Name == "new"));

            pane.Composer.Text = "/cl";
            Dispatcher.UIThread.RunJobs();
            Check("typing narrows the list to matching commands",
                pane.CommandChoices.Count == 1 && pane.CommandChoices[0].Name == "clear");

            pane.Composer.Text = "/";
            Dispatcher.UIThread.RunJobs();
            pane.Composer.Focus();
            Dispatcher.UIThread.RunJobs();
            window.KeyPress(Key.Tab, RawInputModifiers.None, PhysicalKey.Tab, null);
            Dispatcher.UIThread.RunJobs();
            Check("Tab accepts the highlighted command",
                (pane.Composer.Text ?? "").StartsWith("/new", StringComparison.Ordinal));
            Check("accepting a command closes the list", !pane.CommandsVisible);

            pane.Composer.Text = "/he";
            Dispatcher.UIThread.RunJobs();
            pane.Composer.Focus();
            Dispatcher.UIThread.RunJobs();
            window.KeyPress(Key.Escape, RawInputModifiers.None, PhysicalKey.Escape, null);
            Dispatcher.UIThread.RunJobs();
            Check("Escape dismisses the command list", !pane.CommandsVisible);
            Check("Escape leaves the typed text alone",
                (pane.Composer.Text ?? "").StartsWith("/he", StringComparison.Ordinal));
        }
        finally
        {
            Smoke.CloseForTest(window);
        }
    }

    private static void PressEnter(MainWindow window, AssistantPane pane, bool shift)
    {
        pane.Composer.Focus();
        Dispatcher.UIThread.RunJobs();
        window.KeyPress(Key.Enter, shift ? RawInputModifiers.Shift : RawInputModifiers.None,
            PhysicalKey.Enter, null);
        Dispatcher.UIThread.RunJobs();
    }

    private static string TextShown(MainWindow window) =>
        string.Concat(Smoke.Find<TextBlock>(window).Select(block =>
            block.Text is { Length: > 0 } text
                ? text
                : block.Inlines is null
                    ? ""
                    : string.Concat(block.Inlines.OfType<Run>().Select(run => run.Text))));

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

    private static void QuickStartShowsOnceOnFirstOpen()
    {
        Settings.TrySet("assistant.quick_start_done", "", out _);
        var window = new MainWindow();
        window.Show();
        try
        {
            AssistantPane pane = window.AssistantUiForTest.Pane;
            Check("the quick start is hidden before the chat opens", !pane.QuickStartVisible);

            OpenChat(window);
            Check("the first chat open shows the quick start", pane.QuickStartVisible);
            Check("the quick start lists the commands",
                TextShown(window).Contains("/new, /clear, /model", StringComparison.Ordinal));

            Smoke.Find<Button>(window).Single(button => button.Content?.ToString() == "Got it")
                .RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Dispatcher.UIThread.RunJobs();
            Check("dismissing the quick start hides it", !pane.QuickStartVisible);
            Check("dismissing the quick start is remembered",
                Settings.Get("assistant.quick_start_done") == "1");

            OpenChat(window);
            Check("the quick start does not come back on the next open", !pane.QuickStartVisible);
        }
        finally
        {
            Smoke.CloseForTest(window);
            Settings.TrySet("assistant.quick_start_done", "", out _);
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
            Check("the model control appears once the catalog is known", pane.ModelSelectorVisible);
            Check("the model palette lists every catalog model", pane.ModelPicker.VisibleModels.Count == 3);
            Check("no model is forced before the user chooses", pane.SelectedModelItem is null);
            Check("the account line names the Codex config default",
                window.AssistantUiForTest.AccountStatus.Contains("Codex config default", StringComparison.Ordinal));

            pane.ModelButton.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Dispatcher.UIThread.RunJobs();
            Check("the model control opens the palette", pane.ModelPanelOpen);

            Check("the palette groups models under their provider",
                pane.ModelPicker.VisibleModels.Contains("gpt-6-astra"));
            pane.ModelPicker.SetQuery("astra");
            Check("the palette search filters the list",
                pane.ModelPicker.VisibleModels.SequenceEqual(new[] { "gpt-6-astra" }));
            pane.ModelPicker.SetQuery("nothing matches this");
            Check("an empty result is reported rather than showing everything",
                pane.ModelPicker.VisibleModels.Count == 0);
            pane.ModelPicker.SetQuery("");

            Check("choosing from the palette is applied", pane.ModelPicker.Choose("gpt-6-astra"));
            Dispatcher.UIThread.RunJobs();
            Check("choosing a model persists it", Settings.Get("assistant.model") == "gpt-6-astra");
            Check("choosing a model selects it on the connection", connection.SelectedModel == "gpt-6-astra");
            Check("choosing a model shows the chosen model", pane.SelectedModelItem == "gpt-6-astra");
            Check("choosing a model offers a reset", pane.ModelResetVisible);
            Check("the model button names the chosen model",
                pane.ModelButtonLabel.Contains("GPT-6-Astra", StringComparison.Ordinal));

            Check("a model can be favourited", ToggleFavorite(pane, "gpt-6-astra"));
            Check("the favourites section keeps it", AssistantModelPrefs.IsFavorite("gpt-6-astra"));
            Check("toggling a favourite does not change the model",
                Settings.Get("assistant.model") == "gpt-6-astra");
            ToggleFavorite(pane, "gpt-6-astra");
            Check("a favourite can be removed", !AssistantModelPrefs.IsFavorite("gpt-6-astra"));

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

    private static bool ToggleFavorite(AssistantPane pane, string model)
    {
        bool before = AssistantModelPrefs.IsFavorite(model);
        pane.FavoriteForTest(model);
        Dispatcher.UIThread.RunJobs();
        return AssistantModelPrefs.IsFavorite(model) != before;
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
            Check("settings offers the provider", settings!.ProviderSelector.ItemCount == 5);
            Check("settings offers the model control", settings.ModelSelector != null);
            Check("the settings model control is the shared palette",
                settings.ModelSelector.VisibleModels.Count == 17);
            settings.ModelSelector.SetAgentFilter("opencode");
            Check("the settings palette filters by agent like the pane",
                settings.ModelSelector.VisibleModels.Count == 12);
            settings.ModelSelector.SetAgentFilter("");
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

    private static void AssistantRepliesRenderAsMarkdown()
    {
        var window = new MainWindow();
        window.Show();
        try
        {
            OpenChat(window);
            var fake = new FakeChatClient(new ChatResponse(new ChatMessage(ChatRole.Assistant,
                "**bgs_check_project** and `bgs_inspect_object`\n\n- first\n- second")));
            AssistantTools tools = window.CreateAssistantTools();
            window.AssistantUiForTest.SetSessionForTest(window.CreateAssistantSession(fake, tools), tools);
            window.AssistantUiForTest.Pane.Composer.Text = "what can you do";
            Smoke.Find<Button>(window).Single(button => button.Content?.ToString() == "Send")
                .RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Dispatcher.UIThread.RunJobs();

            List<TextBlock> blocks = Smoke.Find<TextBlock>(window).ToList();
            string shown = string.Concat(blocks.Select(Text));

            static string Text(TextBlock block) =>
                block.Text is { Length: > 0 } text ? text
                : block.Inlines is null ? ""
                : string.Concat(block.Inlines.OfType<Run>().Select(run => run.Text));
            Check("a markdown reply shows no literal emphasis markers",
                !shown.Contains("**", StringComparison.Ordinal));
            Check("the bold tool name becomes a bold run",
                blocks.Any(block => block.Inlines is not null && block.Inlines.OfType<Run>()
                    .Any(run => run.Text == "bgs_check_project" && run.FontWeight == FontWeight.SemiBold)));
            Check("inline code becomes a code run",
                blocks.Any(block => block.Inlines is not null && block.Inlines.OfType<Run>()
                    .Any(run => run.Text == "bgs_inspect_object" && run.FontFamily != null)));
            Check("bullets are drawn as bullets", shown.Contains('\u2022', StringComparison.Ordinal));
            Check("the bullet items are rendered", shown.Contains("first", StringComparison.Ordinal));
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
            window.AssistantUiForTest.OpenSettings();
            Dispatcher.UIThread.RunJobs();
            AssistantSettingsWindow settings = window.AssistantUiForTest.SettingsWindowForTest!;
            Check("the provider selector lives in the settings window",
                settings.ProviderSelector.ItemCount == 5);
            string[] offered = settings.ProviderSelector.ItemsSource!.Cast<AssistantProviderItem>()
                .Select(item => item.Wire).ToArray();
            Check("every provider is offered exactly once",
                offered.SequenceEqual(new[] { "codex", "claude", "opencode", "gemini", "local" }));
            Check("the assistant starts on Codex", settings.SelectedProvider == "codex");
            Check("Codex keeps the ChatGPT sign-in", window.AssistantUiForTest.SignInVisible);
            Check("the chat header no longer carries provider clutter",
                !pane.ChatTitle.Contains("detected", StringComparison.OrdinalIgnoreCase));

            settings.ProviderSelector.SelectedItem = settings.ProviderSelector.ItemsSource!
                .Cast<AssistantProviderItem>().Single(item => item.Wire == "claude");
            Dispatcher.UIThread.RunJobs();
            Check("choosing a provider persists it", Settings.Get("assistant.backend") == "claude");
            Check("choosing a provider is reported",
                window.AssistantUiForTest.Status.Contains("Claude Code", StringComparison.Ordinal));
            Check("the provider description follows the choice",
                window.AssistantUiForTest.ProviderStatus.Contains("Claude Code", StringComparison.Ordinal));
            Check("Claude uses its own sign-in rather than BGS",
                window.AssistantUiForTest.AccountStatus.Contains("claude", StringComparison.OrdinalIgnoreCase));
            Check("the ChatGPT sign-in buttons are withdrawn for a CLI provider",
                !window.AssistantUiForTest.SignInVisible);
            Check("the notice explains the CLI-managed sign-in",
                window.AssistantUiForTest.NoticeText.Contains("own command-line tool", StringComparison.Ordinal));
            Check("the settings window follows the chosen provider",
                settings.SelectedProvider == "claude" &&
                settings.AccountText.Contains("claude", StringComparison.OrdinalIgnoreCase));

            pane.ModelPicker.SetAgentFilter("Claude");
            Check("Claude offers a real model list rather than free text",
                pane.ModelPicker.VisibleModels.Count == 2);
            Check("the Claude list is the catalog's list",
                pane.ModelPicker.VisibleModels.SequenceEqual(
                    new[] { "claude-opus-5", "claude-haiku-4-5-20251001" }));
            pane.ModelPicker.SetAgentFilter("");
            Check("an unset Claude model names the CLI default",
                pane.ModelButtonLabel.Contains("Claude CLI default", StringComparison.Ordinal));

            Check("a Claude model can be chosen from the palette", pane.ModelPicker.Choose("claude-opus-5"));
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
                restored.AssistantUiForTest.Backend == AssistantBackend.Claude);
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

    private sealed class GatedChatClient : IChatClient
    {
        private readonly TaskCompletionSource<ChatResponse> _first;
        private readonly Queue<ChatResponse> _rest;
        private int _calls;

        internal GatedChatClient(TaskCompletionSource<ChatResponse> first, params ChatResponse[] rest)
        {
            _first = first;
            _rest = new Queue<ChatResponse>(rest);
        }

        public Task<ChatResponse> GetResponseAsync(
            IEnumerable<ChatMessage> messages, ChatOptions? options = null,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (Interlocked.Increment(ref _calls) == 1) return _first.Task;
            return Task.FromResult(_rest.Count > 0
                ? _rest.Dequeue()
                : new ChatResponse(new ChatMessage(ChatRole.Assistant, "Done.")));
        }

        public async IAsyncEnumerable<ChatResponseUpdate> GetStreamingResponseAsync(
            IEnumerable<ChatMessage> messages, ChatOptions? options = null,
            [System.Runtime.CompilerServices.EnumeratorCancellation]
            CancellationToken cancellationToken = default)
        {
            await Task.CompletedTask;
            yield break;
        }

        public object? GetService(Type serviceType, object? serviceKey = null) => null;
        public void Dispose() { }
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