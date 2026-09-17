using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Threading;

namespace BehaviourStudio.App;

internal sealed partial class AssistantUi
{
    private const string BackendSetting = "assistant.backend";

    private CodexAssistantConnection? _connection;
    private BgsMcpBridge? _bridge;
    private AssistantCli? _cli;
    private AssistantBackend _backend = AssistantBackend.Codex;
    private IReadOnlyList<CodexModel> _providerModels = Array.Empty<CodexModel>();
    private AssistantSettingsWindow? _settingsWindow;
    private readonly AssistantSettingsState _settingsState = new();
    private string _providerDescription = "";
    private string _accountText = "";
    private string _deviceCode = "";

    internal void OpenSettings()
    {
        _settingsState.ChooseProvider = SelectProvider;
        _settingsState.ChooseModel = SelectModel;
        _settingsState.ResetModel = ResetModel;
        _settingsState.SignIn = SignIn;
        _settingsState.DeviceCode = DeviceCode;
        _settingsState.SignOut = SignOut;
        if (_settingsWindow is null || !_settingsWindow.IsVisible)
        {
            _settingsWindow?.Close();
            _settingsWindow = new AssistantSettingsWindow(_settingsState);
        }
        RefreshSettingsWindow();
        _settingsWindow.Present(_owner);
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
        _settingsState.Models = _providerModels;
        _settingsState.SelectedModel = Settings.Get(ModelSettingKey(_backend));
        _settingsState.DefaultModelLabel = DefaultModelLabel();
        _settingsWindow?.Refresh();
    }

    internal static string ModelSettingKey(AssistantBackend backend) => backend switch
    {
        AssistantBackend.Claude => "assistant.model.claude",
        AssistantBackend.Opencode => "assistant.model.opencode",
        _ => "assistant.model",
    };

    internal AssistantBackend Backend => _backend;

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
        if (!EnsureCli()) return null;
        BgsMcpBridge bridge = EnsureBridge(tools);
        string model = Settings.Get(ModelSettingKey(_backend));
        return _backend == AssistantBackend.Claude
            ? new ClaudeAssistantSession(_cli!, bridge, model, () => tools.HasPendingApproval)
            : new OpencodeAssistantSession(_cli!, bridge, model, () => tools.HasPendingApproval);
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

        string name = AssistantCliLocator.DisplayName(_backend);
        bool found = AssistantCliLocator.TryLocateFromSettings(_backend, out _, out _);
        _providerDescription = found ? $"{name} · CLI detected" : $"{name} · CLI not detected";
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

    private void RefreshModelSelector()
    {
        if (_backend == AssistantBackend.Codex)
        {
            if (_connection is null) return;
            _providerModels = _connection.Models;
        }
        else if (_backend == AssistantBackend.Claude && _connection is null && _providerModels.Count == 0)
        {
            _providerModels = ClaudeModelCatalog.Read();
        }
        _pane.SetModels(_providerModels, Settings.Get(ModelSettingKey(_backend)), DefaultModelLabel());
        RefreshSettingsWindow();
    }

    private string DefaultModelLabel() => _backend switch
    {
        AssistantBackend.Claude => "Claude CLI default",
        AssistantBackend.Opencode => "opencode default",
        _ => "Codex config default",
    };

    private async void SignIn()
    {
        try
        {
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
