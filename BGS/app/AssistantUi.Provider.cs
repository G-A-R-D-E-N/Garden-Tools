using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Threading;
using Microsoft.Extensions.AI;

namespace BehaviourStudio.App;

internal sealed partial class AssistantUi
{
    private const string BackendSetting = "assistant.backend";

    internal static Func<Task<IReadOnlyList<CodexModel>>>? AllAgentsForTest;

    private CodexAssistantConnection? _connection;
    private BgsMcpBridge? _bridge;
    private AssistantCli? _cli;
    private readonly Dictionary<AssistantBackend, string> _apiKeys = new();
    private AssistantBackend _backend = AssistantBackend.Codex;
    private IReadOnlyList<CodexModel> _providerModels = Array.Empty<CodexModel>();
    private IReadOnlyList<CodexModel> _catalogModels = Array.Empty<CodexModel>();
    private bool _catalogLoading;
    private AssistantSettingsWindow? _settingsWindow;
    private readonly AssistantSettingsState _settingsState = new();
    private string _providerDescription = "";
    private string _accountText = "";
    private string _deviceCode = "";

    internal void OpenSettings()
    {
        _settingsState.ChooseProvider = SelectProvider;
        _settingsState.ChooseModel = SelectCatalogModel;
        _settingsState.ResetModel = ResetModel;
        _settingsState.SignIn = SignIn;
        _settingsState.DeviceCode = DeviceCode;
        _settingsState.SignOut = SignOut;
        _settingsState.ChooseApiKey = ChooseApiKey;
        _settingsState.ChooseBaseUrl = ChooseBaseUrl;
        if (_settingsWindow is null || !_settingsWindow.IsVisible)
        {
            _settingsWindow?.Close();
            _settingsWindow = new AssistantSettingsWindow(_settingsState);
        }
        RefreshSettingsWindow();
        _settingsWindow.Present(_owner);
        _ = RefreshCatalogAsync();
    }

    internal AssistantSettingsWindow? SettingsWindowForTest => _settingsWindow;

    private void RefreshSettingsWindow()
    {
        _settingsState.SelectedBackend = AssistantCliLocator.CommandName(_backend);
        _settingsState.Description = _providerDescription;
        _settingsState.Account = _accountText;
        _settingsState.DeviceCodeText = _deviceCode;
        _settingsState.CodexManaged = _backend == AssistantBackend.Codex;
        _settingsState.SignedIn = _backend == AssistantBackend.Codex &&
                                  _connection?.Account.SignedIn == true;
        _settingsState.Models = CurrentCatalog();
        _settingsState.SelectedModel = Settings.Get(ModelSettingKey(_backend));
        _settingsState.DefaultModelLabel = DefaultModelLabel();
        _settingsState.IsApiProvider = ApiProviders.IsApi(_backend);
        _settingsState.BaseUrl = ApiProviders.IsApi(_backend) ? ApiProviders.BaseUrl(_backend) : "";
        _settingsState.ApiKeySet = KeyFor(_backend).Length > 0;
        _settingsWindow?.Refresh();
    }

    private void ChooseApiKey(string key)
    {
        _apiKeys[_backend] = (key ?? "").Trim();
        RefreshProviderStatus();
        RefreshAccountText();
        _ = RefreshProviderModelsAsync();
    }

    private void ChooseBaseUrl(string url)
    {
        string value = (url ?? "").Trim();
        if (!Settings.TrySet(ApiProviders.BaseUrlSetting(_backend), value, out string failure))
        {
            _pane.SetStatus(failure, Ux.WarnBrush);
            return;
        }
        RefreshProviderStatus();
        _ = RefreshProviderModelsAsync();
        _ = RefreshCatalogAsync();
    }

    private IReadOnlyList<CodexModel> CurrentCatalog() =>
        _catalogModels.Count > 0 ? _catalogModels : _providerModels;

    internal static string ModelSettingKey(AssistantBackend backend) => backend switch
    {
        AssistantBackend.Claude => "assistant.model.claude",
        AssistantBackend.Opencode => "assistant.model.opencode",
        AssistantBackend.Gemini => "assistant.model.gemini",
        AssistantBackend.Local => "assistant.model.local",
        _ => "assistant.model",
    };

    internal AssistantBackend Backend => _backend;
    internal string ApiKeyForTest => KeyFor(_backend);
    internal void SetApiKeyForTest(string key) => _apiKeys[_backend] = (key ?? "").Trim();

    private string KeyFor(AssistantBackend backend) =>
        _apiKeys.TryGetValue(backend, out string? key) ? key : "";

    private string ApiKeyFor(AssistantBackend backend)
    {
        string key = KeyFor(backend);
        if (key.Length == 0) return "";
        return ApiProviders.TryBaseUrl(backend, out Uri? endpoint, out _) &&
               ApiProviders.AllowsCredentials(endpoint!)
            ? key
            : "";
    }

    private void InitializeProvider()
    {
        _backend = AssistantCliLocator.FromSettings();
        RefreshProviderStatus();
        RefreshAccountText();
        RefreshModelSelector();
        _ = RefreshProviderModelsAsync();
    }

    private IAssistantSession? CreateSessionForChat(string chatId)
    {
        AssistantTools tools = EnsureTools();
        if (_backend == AssistantBackend.Codex)
        {
            if (!EnsureCodex()) return null;
            return new CodexAssistantSession(_connection!, tools.Functions, () => tools.HasPendingApproval);
        }
        if (ApiProviders.IsApi(_backend)) return CreateApiSession(tools);
        if (!EnsureCli()) return null;
        BgsMcpBridge bridge = EnsureBridge(tools);
        string model = Settings.Get(ModelSettingKey(_backend));
        return _backend == AssistantBackend.Claude
            ? new ClaudeAssistantSession(_cli!, bridge, model, () => tools.HasPendingApproval)
            : new OpencodeAssistantSession(_cli!, bridge, model, () => tools.HasPendingApproval);
    }

    private AssistantSession? CreateApiSession(AssistantTools tools)
    {
        if (!ApiProviders.TryBaseUrl(_backend, out Uri? endpoint, out string failure))
        {
            _pane.SetStatus(failure, Ux.WarnBrush);
            return null;
        }
        string model = Settings.Get(ModelSettingKey(_backend));
        if (model.Length == 0)
        {
            _pane.SetStatus("Choose a model for this provider first.", Ux.WarnBrush);
            return null;
        }
        IChatClient client = ApiChatClients.Create(endpoint!, ApiKeyFor(_backend), model);
        return new AssistantSession(client, tools.Functions, () => tools.HasPendingApproval);
    }

    private bool EnsureCli()
    {
        if (_cli is not null) return true;
        if (!AssistantCliLocator.TryLocateFromSettings(_backend, out AssistantCli? cli, out string failure))
        {
            _pane.SetStatus(failure, Ux.WarnBrush);
            return false;
        }
        _cli = cli;
        return true;
    }

    private BgsMcpBridge EnsureBridge(AssistantTools tools)
    {
        if (_bridge is not null) return _bridge;
        _bridge = BgsMcpBridge.Start(tools.Functions);
        BgsMcpBridge bridge = _bridge;
        _owner.Closed += (_, _) => bridge.Dispose();
        return _bridge;
    }

    private bool EnsureCodex()
    {
        if (_connection != null) return true;
        AssistantProviderOptions options = AssistantProviderOptions.FromSettings();
        if (!AssistantProvider.TryResolveExecutable(options, out _, out string error))
        {
            _pane.SetStatus(error, Ux.WarnBrush);
            return false;
        }
        _connection = _owner.CreateCodexConnection(options);
        _connection.Changed += OnConnectionChanged;
        return true;
    }

    private void OnConnectionChanged() => Dispatcher.UIThread.Post(RefreshAccountText);

    private void SelectProvider(string backendName)
    {
        AssistantBackend? backend = AssistantCliLocator.Parse(backendName);
        if (backend is null || backend == _backend) return;
        if (_controller?.List.IsBusy == true)
        {
            _pane.SetStatus("Wait for the current request to finish before switching provider.", Ux.WarnBrush);
            return;
        }
        if (!Settings.TrySet(BackendSetting, backendName, out string failure))
        {
            _pane.SetStatus(failure, Ux.WarnBrush);
            return;
        }

        _backend = backend.Value;
        _cli = null;
        _providerModels = Array.Empty<CodexModel>();
        _controller?.EndAllSessions();
        _pane.SetApproval(false);
        RefreshProviderStatus();
        RefreshAccountText();
        RefreshModelSelector();
        _ = RefreshProviderModelsAsync();
        _ = RefreshCatalogAsync();
        _pane.SetStatus("Provider set to " + AssistantCliLocator.DisplayName(_backend) + ".",
            new Avalonia.Media.SolidColorBrush(Ux.Good));
    }

    private void RefreshProviderStatus()
    {
        if (_backend == AssistantBackend.Codex)
        {
            AssistantProviderOptions options = AssistantProviderOptions.FromSettings();
            bool detected = AssistantProvider.TryResolveExecutable(options, out CodexExecutable? executable, out _);
            _providerDescription = AssistantProvider.Describe(options, detected ? executable : null);
            RefreshSettingsWindow();
            return;
        }

        if (ApiProviders.IsApi(_backend))
        {
            ApiProviderPreset preset = ApiProviders.For(_backend)!;
            string apiBaseUrl = ApiProviders.BaseUrl(_backend);
            if (apiBaseUrl.Length == 0)
                _providerDescription = preset.Name + " \u00B7 set the API base URL";
            else if (_backend == AssistantBackend.Gemini && KeyFor(_backend).Length == 0)
                _providerDescription = preset.Name + " \u00B7 API key needed for this session";
            else
                _providerDescription = preset.Name + " \u00B7 " + apiBaseUrl;
            RefreshSettingsWindow();
            return;
        }

        string name = AssistantCliLocator.DisplayName(_backend);
        bool found = AssistantCliLocator.TryLocateFromSettings(_backend, out _, out _);
        _providerDescription = found ? $"{name} \u00B7 CLI detected" : $"{name} \u00B7 CLI not detected";
        RefreshSettingsWindow();
    }

    private async Task RefreshProviderModelsAsync()
    {
        if (_backend == AssistantBackend.Codex)
        {
            if (_connection is null || !_connection.IsConnected) return;
            _providerModels = _connection.Models;
            RefreshModelSelector();
            return;
        }
        if (ApiProviders.IsApi(_backend))
        {
            await RefreshApiModelsAsync().ConfigureAwait(true);
            return;
        }
        AssistantBackend requested = _backend;
        IReadOnlyList<CodexModel> models = _backend == AssistantBackend.Claude
            ? await Task.Run(ClaudeModelCatalog.Read).ConfigureAwait(true)
            : await ReadOpencodeModelsAsync().ConfigureAwait(true);
        if (_backend != requested) return;
        _providerModels = models;
        RefreshModelSelector();
    }

    private async Task<IReadOnlyList<CodexModel>> ReadOpencodeModelsAsync()
    {
        if (!EnsureCli()) return Array.Empty<CodexModel>();
        return await OpencodeModelCatalog.ReadAsync(_cli!).ConfigureAwait(true);
    }

    internal static string ApiAgent(AssistantBackend backend) => backend == AssistantBackend.Gemini
        ? AssistantModelCatalog.GeminiAgent
        : AssistantModelCatalog.LocalAgent;

    internal static IReadOnlyList<CodexModel> ApiModels(AssistantBackend backend, IReadOnlyList<string> ids)
    {
        string agent = ApiAgent(backend);
        return ids.Select(id => new CodexModel(id, id, false, id) { Agent = agent, Group = agent }).ToList();
    }

    private async Task RefreshApiModelsAsync()
    {
        AssistantBackend requested = _backend;
        ApiModelList result = await ApiModelCatalog
            .ReadAsync(ApiProviders.BaseUrl(_backend), ApiKeyFor(_backend)).ConfigureAwait(true);
        if (_backend != requested) return;
        if (result.Failed)
        {
            _providerModels = Array.Empty<CodexModel>();
            _pane.SetStatus(result.Error, Ux.WarnBrush);
            RefreshModelSelector();
            return;
        }
        _providerModels = ApiModels(_backend, result.ModelIds);
        RefreshModelSelector();
    }

    private void RefreshModelSelector()
    {
        if (_backend == AssistantBackend.Codex)
        {
            if (_connection is not null) _providerModels = _connection.Models;
        }
        else if (_backend == AssistantBackend.Claude && _connection is null && _providerModels.Count == 0)
        {
            _providerModels = ClaudeModelCatalog.Read();
        }
        _pane.SetModels(CurrentCatalog(), AssistantModelCatalog.AgentFor(_backend),
            Settings.Get(ModelSettingKey(_backend)), DefaultModelLabel());
        RefreshSettingsWindow();
    }

    private async Task RefreshCatalogAsync()
    {
        if (_catalogLoading) return;
        _catalogLoading = true;
        try
        {
            if (AllAgentsForTest is not null)
            {
                IReadOnlyList<CodexModel> synthetic = await AllAgentsForTest().ConfigureAwait(true);
                if (synthetic.Count > 0) _catalogModels = synthetic;
                RefreshModelSelector();
                return;
            }

            var sources = new List<IReadOnlyList<CodexModel>>();
            IReadOnlyList<CodexModel> claude = await Task.Run(ClaudeModelCatalog.Read).ConfigureAwait(true);
            if (claude.Count > 0) sources.Add(claude);

            if (AssistantCliLocator.TryLocateFromSettings(
                    AssistantBackend.Opencode, out AssistantCli? opencode, out _) && opencode is not null)
            {
                IReadOnlyList<CodexModel> models =
                    await OpencodeModelCatalog.ReadAsync(opencode).ConfigureAwait(true);
                if (models.Count > 0) sources.Add(models);
            }

            IReadOnlyList<CodexModel>? codex = await TryReadCodexModelsAsync().ConfigureAwait(true);
            if (codex is { Count: > 0 }) sources.Add(codex);

            foreach (AssistantBackend api in new[] { AssistantBackend.Gemini, AssistantBackend.Local })
            {
                if (api == AssistantBackend.Gemini && KeyFor(api).Length == 0) continue;
                ApiModelList list = await ApiModelCatalog
                    .ReadAsync(ApiProviders.BaseUrl(api), ApiKeyFor(api)).ConfigureAwait(true);
                if (list.ModelIds.Count > 0) sources.Add(ApiModels(api, list.ModelIds));
            }

            IReadOnlyList<CodexModel> merged = AssistantModelCatalog.Merge(sources);
            if (merged.Count > 0) _catalogModels = merged;
            RefreshModelSelector();
        }
        finally
        {
            _catalogLoading = false;
        }
    }

    private async Task<IReadOnlyList<CodexModel>?> TryReadCodexModelsAsync()
    {
        if (_connection is null)
        {
            if (_backend == AssistantBackend.Codex) return null;
            AssistantProviderOptions options = AssistantProviderOptions.FromSettings();
            if (!AssistantProvider.TryResolveExecutable(options, out _, out _)) return null;
            _connection = _owner.CreateCodexConnection(options);
            _connection.Changed += OnConnectionChanged;
        }
        try
        {
            await _connection.ConnectAsync().ConfigureAwait(true);
            return _connection.Models;
        }
        catch (Exception exception) when (
            exception is CodexProtocolException or InvalidOperationException or TimeoutException)
        {
            return null;
        }
    }

    private void SelectCatalogModel(CodexModel model)
    {
        if (_controller?.List.IsBusy == true)
        {
            _pane.SetStatus("Wait for the current request to finish before switching models.", Ux.WarnBrush);
            return;
        }
        AssistantBackend? target = AssistantCliLocator.Parse(model.Agent);
        if (target is null) return;
        if (target.Value != _backend)
        {
            if (!Settings.TrySet(BackendSetting, AssistantCliLocator.CommandName(target.Value), out string failure))
            {
                _pane.SetStatus(failure, Ux.WarnBrush);
                return;
            }
            _backend = target.Value;
            _cli = null;
            _providerModels = Array.Empty<CodexModel>();
            _controller?.EndAllSessions();
            _pane.SetApproval(false);
            RefreshProviderStatus();
            RefreshAccountText();
            _providerModels = _catalogModels
                .Where(candidate => AssistantCliLocator.Parse(AssistantModelCatalog.AgentOf(candidate)) == _backend)
                .ToList();
        }
        SelectModel(model.WireModel);
    }

    private string DefaultModelLabel() => _backend switch
    {
        AssistantBackend.Claude => "Claude CLI default",
        AssistantBackend.Opencode => "opencode default",
        AssistantBackend.Gemini => ApiProviders.Gemini.ModelLabel,
        AssistantBackend.Local => ApiProviders.Local.ModelLabel,
        _ => "Codex config default",
    };

    private async void SignIn()
    {
        try
        {
            if (ApiProviders.IsApi(_backend))
            {
                _pane.SetStatus(
                    _backend == AssistantBackend.Gemini
                        ? "Paste a Gemini API key in Assistant settings. It is kept only for this session."
                        : "Local endpoints do not sign in.",
                    Ux.MetaBrush);
                return;
            }
            if (_backend != AssistantBackend.Codex)
            {
                _pane.SetStatus(
                    $"{AssistantCliLocator.DisplayName(_backend)} signs in through its own CLI, not through BGS.",
                    Ux.MetaBrush);
                return;
            }
            if (!EnsureCodex()) return;
            CodexAccount account = await _connection!.ConnectAsync().ConfigureAwait(true);
            RefreshAccountText();
            if (account.SignedIn)
            {
                _pane.SetStatus("Connected. Opening the assistant made no model request.", Ux.MetaBrush);
                return;
            }
            CodexLoginChallenge challenge = await _connection.BeginChatGptLoginAsync().ConfigureAwait(true);
            if (challenge.HasBrowserUrl)
            {
                OpenUrl(challenge.AuthUrl);
                _pane.SetStatus("Waiting for browser sign-in to finish...", Ux.MetaBrush);
            }
            else
            {
                _pane.SetStatus("Waiting for sign-in to finish...", Ux.MetaBrush);
            }
        }
        catch (CodexProtocolException exception)
        {
            _pane.SetStatus("Disconnected: " + exception.Message, Ux.WarnBrush);
        }
        catch (Exception exception) when (exception is InvalidOperationException or TimeoutException)
        {
            _pane.SetStatus("Disconnected: Codex sign-in could not be started.", Ux.WarnBrush);
        }
        finally
        {
        }
    }

    private async void DeviceCode()
    {
        try
        {
            if (ApiProviders.IsApi(_backend))
            {
                _pane.SetStatus("This provider has no device-code sign-in.", Ux.MetaBrush);
                return;
            }
            if (_backend != AssistantBackend.Codex)
            {
                _pane.SetStatus(
                    $"{AssistantCliLocator.DisplayName(_backend)} signs in through its own CLI, not through BGS.",
                    Ux.MetaBrush);
                return;
            }
            if (!EnsureCodex()) return;
            await _connection!.ConnectAsync().ConfigureAwait(true);
            CodexLoginChallenge challenge = await _connection.BeginDeviceCodeLoginAsync().ConfigureAwait(true);
            _deviceCode = challenge.UserCode.Length > 0
                ? "Enter code " + challenge.UserCode + " at " + challenge.VerificationUrl
                : challenge.VerificationUrl;
            RefreshSettingsWindow();
            OpenUrl(challenge.VerificationUrl);
            _pane.SetStatus("Waiting for device-code sign-in to finish...", Ux.MetaBrush);
        }
        catch (CodexProtocolException exception)
        {
            _pane.SetStatus("Disconnected: " + exception.Message, Ux.WarnBrush);
        }
        catch (Exception exception) when (exception is InvalidOperationException or TimeoutException)
        {
            _pane.SetStatus("Disconnected: device-code sign-in could not be started.", Ux.WarnBrush);
        }
        finally
        {
        }
    }

    private async void SignOut()
    {
        try
        {
            _requestCancellation?.Cancel();
            _controller?.EndAllSessions();
            _pane.SetApproval(false);
            if (ApiProviders.IsApi(_backend))
            {
                _apiKeys.Remove(_backend);
                RefreshAccountText();
                _pane.SetStatus("Cleared the session API key.", Ux.MetaBrush);
                return;
            }
            if (_backend != AssistantBackend.Codex || _connection is null)
            {
                _pane.SetStatus("Every chat will start fresh on the next request.", Ux.MetaBrush);
                return;
            }
            await _connection.SignOutAsync().ConfigureAwait(true);
            RefreshAccountText();
            _pane.SetStatus(
                "Signed out. Every chat will start a fresh ephemeral thread after you sign in again.",
                Ux.MetaBrush);
        }
        catch (CodexProtocolException exception)
        {
            _pane.SetStatus("Disconnected: " + exception.Message, Ux.WarnBrush);
        }
        finally
        {
        }
    }

    private void RefreshAccountText()
    {
        if (_controller is null) return;
        if (ApiProviders.IsApi(_backend))
        {
            _accountText = _backend == AssistantBackend.Gemini
                ? (KeyFor(_backend).Length > 0
                    ? "Gemini API key set for this session"
                    : "No Gemini API key in this session")
                : "Local endpoint \u00B7 no account";
            RefreshSettingsWindow();
            return;
        }
        if (_backend != AssistantBackend.Codex)
        {
            _accountText = "Signed in through the " + AssistantCliLocator.CommandName(_backend) + " CLI";
            RefreshSettingsWindow();
            return;
        }
        if (_connection is null || !_connection.IsConnected)
        {
            _accountText = "Signed out";
            RefreshSettingsWindow();
            return;
        }
        CodexAccount account = _connection.Account;
        if (!account.SignedIn)
        {
            _accountText = "Signed out";
            RefreshSettingsWindow();
            return;
        }

        string plan = PlanLabel(account.PlanType);
        _accountText = plan.Length > 0
            ? $"Connected · ChatGPT {plan}"
            : $"Connected · {account.AuthMode}";
        if (_connection.ModelLabel.Length > 0) _accountText += $"\nModel: {_connection.ModelLabel}";
        RefreshModelSelector();
        RefreshSettingsWindow();
    }

    private void SelectModel(string wireModel)
    {
        if (_controller?.List.IsBusy == true)
        {
            _pane.SetStatus("Wait for the current request to finish before switching models.", Ux.WarnBrush);
            return;
        }
        if (string.Equals(wireModel, Settings.Get(ModelSettingKey(_backend)), StringComparison.Ordinal)) return;
        if (_backend == AssistantBackend.Codex)
        {
            if (_connection is null) return;
            if (_connection.TrySelectModel(wireModel, out string codexFailure))
                _pane.SetStatus("Model set for the next request on every chat.",
                    new Avalonia.Media.SolidColorBrush(Ux.Good));
            else
                _pane.SetStatus(codexFailure, Ux.WarnBrush);
            RefreshAccountText();
            return;
        }

        if (ApiProviders.IsApi(_backend))
        {
            if (!Settings.TrySet(ModelSettingKey(_backend), wireModel, out string apiFailure))
            {
                _pane.SetStatus(apiFailure, Ux.WarnBrush);
                return;
            }
            _pane.SetStatus(
                wireModel.Length > 0 ? "Model set for the next request." : "Using the provider default.",
                new Avalonia.Media.SolidColorBrush(Ux.Good));
            RefreshModelSelector();
            return;
        }

        if (_backend == AssistantBackend.Opencode && wireModel.Length > 0 &&
            !_providerModels.Any(model => model.WireModel == wireModel))
        {
            _pane.SetStatus("That model is not in the opencode catalog.", Ux.WarnBrush);
            RefreshModelSelector();
            return;
        }
        if (!Settings.TrySet(ModelSettingKey(_backend), wireModel, out string failure))
        {
            _pane.SetStatus(failure, Ux.WarnBrush);
            return;
        }
        _pane.SetStatus(
            wireModel.Length > 0 ? "Model set for the next request." : "Using the provider default.",
            new Avalonia.Media.SolidColorBrush(Ux.Good));
        RefreshModelSelector();
    }

    private void ResetModel() => SelectModel("");

    internal static string PlanLabel(string planType) =>
        planType.Length == 0
            ? ""
            : char.ToUpperInvariant(planType[0]) + planType[1..].ToLowerInvariant();

    private static void OpenUrl(string url)
    {
        if (string.IsNullOrWhiteSpace(url)) return;
        try
        {
            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
            {
                FileName = url,
                UseShellExecute = true,
            });
        }
        catch (Exception exception) when (exception is InvalidOperationException or System.ComponentModel.Win32Exception)
        {
        }
    }
}
