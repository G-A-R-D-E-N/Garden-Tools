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
        AssistantModelSelector.BindProviders(_pane.ProviderSelector, AssistantCliLocator.CommandName(_backend));
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
            _pane.SetProvider(AssistantProvider.Describe(options, detected ? executable : null));
            return;
        }

        string name = AssistantCliLocator.DisplayName(_backend);
        bool found = AssistantCliLocator.TryLocateFromSettings(_backend, out _, out _);
        _pane.SetProvider(found ? $"{name} · CLI detected" : $"{name} · CLI not detected");
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
        if (_backend == AssistantBackend.Claude)
        {
            _providerModels = Array.Empty<CodexModel>();
            RefreshModelSelector();
            return;
        }
        if (!EnsureCli()) return;
        AssistantBackend requested = _backend;
        IReadOnlyList<CodexModel> models = await OpencodeModelCatalog.ReadAsync(_cli!).ConfigureAwait(true);
        if (_backend != requested) return;
        _providerModels = models;
        RefreshModelSelector();
    }

    private async void SignIn()
    {
        _pane.SetAuthBusy(true);
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
            _pane.SetAuthBusy(false);
        }
    }

    private async void DeviceCode()
    {
        _pane.SetAuthBusy(true);
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
            _pane.SetDeviceCode(challenge.VerificationUrl, challenge.UserCode);
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
            _pane.SetAuthBusy(false);
        }
    }

    private async void SignOut()
    {
        _pane.SetAuthBusy(true);
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
            _pane.SetAuthBusy(false);
        }
    }

    private void RefreshAccountText()
    {
        if (_controller is null) return;
        if (_backend != AssistantBackend.Codex)
        {
            _pane.SetAccount("Signed in through the " + AssistantCliLocator.CommandName(_backend) + " CLI");
            return;
        }
        if (_connection is null || !_connection.IsConnected)
        {
            _pane.SetAccount("Signed out");
            return;
        }
        CodexAccount account = _connection.Account;
        if (!account.SignedIn)
        {
            _pane.SetAccount("Signed out");
            return;
        }

        string plan = PlanLabel(account.PlanType);
        string text = plan.Length > 0
            ? $"Connected · ChatGPT {plan}"
            : $"Connected · {account.AuthMode}";
        if (_connection.ModelLabel.Length > 0) text += $"\nModel: {_connection.ModelLabel}";
        _pane.SetAccount(text);
        RefreshModelSelector();
    }

    private void RefreshModelSelector()
    {
        if (_backend == AssistantBackend.Claude)
        {
            _pane.SetModelEntry(true, Settings.Get(ModelSettingKey(_backend)));
            AssistantModelSelector.Bind(_pane.ModelSelector, _pane, Array.Empty<CodexModel>(), "", "");
            return;
        }
        _pane.SetModelEntry(false, "");
        if (_backend == AssistantBackend.Codex)
        {
            if (_connection is null) return;
            _providerModels = _connection.Models;
        }
        AssistantModelSelector.Bind(_pane.ModelSelector, _pane, _providerModels,
            Settings.Get(ModelSettingKey(_backend)),
            _backend == AssistantBackend.Codex ? "Codex config default" : "opencode default");
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
