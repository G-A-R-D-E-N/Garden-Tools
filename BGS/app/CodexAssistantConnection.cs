using System;
using System.Collections.Generic;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace BehaviourStudio.App;

public delegate ICodexTransport CodexTransportFactory(
    string executablePath, IReadOnlyList<string> arguments);

public sealed class CodexAssistantConnection : IDisposable
{
    private readonly AssistantProviderOptions _options;
    private readonly CodexTransportFactory _transportFactory;
    private readonly SemaphoreSlim _connectGate = new(1, 1);
    private ICodexTransport? _process;
    private CodexAppServerClient? _client;
    private CodexAccountClient? _accounts;
    private CodexAccount _account = CodexAccount.SignedOut;
    private string _model = "";
    private bool _disposed;

    public IReadOnlyList<string> LaunchArguments { get; private set; } = Array.Empty<string>();

    public CodexAssistantConnection(
        AssistantProviderOptions options, CodexTransportFactory? transportFactory = null)
    {
        _options = options ?? throw new ArgumentNullException(nameof(options));
        _transportFactory = transportFactory ?? DefaultTransportFactory;
    }

    private static ICodexTransport DefaultTransportFactory(
        string executablePath, IReadOnlyList<string> arguments) =>
        new CodexAppServerProcess(executablePath, arguments);

    public event Action? Changed;

    public CodexAppServerClient Client =>
        _client ?? throw new InvalidOperationException("the Codex app server is not connected");

    public CodexServerInfo? ServerInfo => _client?.ServerInfo;
    public CodexAccount Account => _account;
    public string ModelLabel => _model;
    public bool IsConnected => _client?.IsConnected == true;
    public bool? DynamicToolsAvailable
    {
        get => _client?.DynamicToolsAvailable;
        set { if (_client != null) _client.DynamicToolsAvailable = value; }
    }

    public async Task<CodexAccount> ConnectAsync(CancellationToken cancellationToken = default)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        await _connectGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (_client is { IsConnected: true }) return _account;

            if (!AssistantProvider.TryResolveExecutable(_options, out CodexExecutable? executable, out string error))
                throw new CodexProtocolException(error);

            var arguments = new List<string>
            {
                "app-server", "--listen", "stdio://", "-c", "mcp_servers={}",
            };
            LaunchArguments = arguments;
            _process = _transportFactory(executable!.Path, arguments);
            _client = new CodexAppServerClient(_process);
            _client.Notification += OnNotification;
            await _client.ConnectAsync(cancellationToken).ConfigureAwait(false);

            _accounts = new CodexAccountClient(_client);
            await RefreshAccountAsync(cancellationToken).ConfigureAwait(false);
            _model = await ReadModelLabelAsync(cancellationToken).ConfigureAwait(false);
            Changed?.Invoke();
            return _account;
        }
        finally
        {
            _connectGate.Release();
        }
    }

    public async Task<CodexAccount> RefreshAccountAsync(CancellationToken cancellationToken = default)
    {
        if (_accounts is null) return CodexAccount.SignedOut;
        try
        {
            _account = await _accounts.ReadAccountAsync(false, cancellationToken).ConfigureAwait(false);
            Changed?.Invoke();
        }
        catch (CodexProtocolException)
        {
            _account = CodexAccount.SignedOut;
        }
        return _account;
    }

    public async Task<CodexLoginChallenge> BeginChatGptLoginAsync(CancellationToken cancellationToken = default)
    {
        await ConnectAsync(cancellationToken).ConfigureAwait(false);
        return await _accounts!.StartChatGptLoginAsync(cancellationToken).ConfigureAwait(false);
    }

    public async Task<CodexLoginChallenge> BeginDeviceCodeLoginAsync(CancellationToken cancellationToken = default)
    {
        await ConnectAsync(cancellationToken).ConfigureAwait(false);
        return await _accounts!.StartDeviceCodeLoginAsync(cancellationToken).ConfigureAwait(false);
    }

    public async Task<bool> CancelLoginAsync(string loginId, CancellationToken cancellationToken = default)
    {
        if (_accounts is null) return false;
        return await _accounts.CancelLoginAsync(loginId, cancellationToken).ConfigureAwait(false);
    }

    public async Task SignOutAsync(CancellationToken cancellationToken = default)
    {
        if (_accounts is null) return;
        await _accounts.LogoutAsync(cancellationToken).ConfigureAwait(false);
        _account = CodexAccount.SignedOut;
        Changed?.Invoke();
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        if (_client != null) _client.Notification -= OnNotification;
        _client?.Dispose();
        _process?.Dispose();
        _connectGate.Dispose();
    }

    private void OnNotification(string method, JsonElement? parameters)
    {
        if (method == CodexMethods.AccountUpdated)
        {
            string authMode = CodexProtocol.String(parameters, "authMode");
            string planType = CodexProtocol.Scrub(CodexProtocol.String(parameters, "planType"), 32);
            _account = authMode.Length == 0
                ? CodexAccount.SignedOut
                : new CodexAccount(authMode, planType, true);
            Changed?.Invoke();
            return;
        }

        if (method == CodexMethods.AccountLoginCompleted)
        {
            if (CodexProtocol.Bool(parameters, "success"))
            {
                _ = RefreshAccountAsync();
            }
            else
            {
                _account = CodexAccount.SignedOut;
                Changed?.Invoke();
            }
        }
    }

    private async Task<string> ReadModelLabelAsync(CancellationToken cancellationToken)
    {
        if (_accounts is null) return "";
        try
        {
            CodexModel? model = await _accounts.ReadDefaultModelAsync(cancellationToken).ConfigureAwait(false);
            return model?.DisplayName ?? "";
        }
        catch (CodexProtocolException)
        {
            return "";
        }
    }
}
