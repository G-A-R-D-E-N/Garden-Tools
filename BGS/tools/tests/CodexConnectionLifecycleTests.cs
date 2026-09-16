using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using BehaviourStudio.App;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class CodexConnectionLifecycleTests
{
    [Fact]
    public async Task RepeatedConnectKeepsExactlyOneTransport()
    {
        var transports = new List<CountingTransport>();
        using var connection = Connection(transports);

        await connection.ConnectAsync();
        await connection.ConnectAsync();

        Assert.Single(transports);
        Assert.Equal(1, transports[0].StartCount);
        Assert.Equal(0, transports[0].DisposeCount);
    }

    [Fact]
    public async Task ReconnectDisposesExitedTransportBeforeReplacement()
    {
        var transports = new List<CountingTransport>();
        using var connection = Connection(transports);

        await connection.ConnectAsync();
        transports[0].Exit();
        await connection.ConnectAsync();

        Assert.Equal(2, transports.Count);
        Assert.Equal(1, transports[0].DisposeCount);
        Assert.False(transports[0].IsRunning);
        Assert.Equal(1, transports[1].StartCount);
        Assert.True(transports[1].IsRunning);
    }

    [Fact]
    public async Task FailedHandshakeDisposesPartialTransport()
    {
        var transports = new List<CountingTransport>();
        using var connection = Connection(transports, failFirstInitialize: true);

        await Assert.ThrowsAsync<CodexProtocolException>(() => connection.ConnectAsync());

        Assert.Single(transports);
        Assert.Equal(1, transports[0].DisposeCount);
        Assert.False(transports[0].IsRunning);
        Assert.False(connection.IsConnected);
    }

    [Fact]
    public async Task AuthOnlyConnectionIsDisposedByOwner()
    {
        var transports = new List<CountingTransport>();
        var connection = Connection(transports);

        await connection.ConnectAsync();
        connection.Dispose();

        Assert.Single(transports);
        Assert.Equal(1, transports[0].DisposeCount);
        Assert.False(transports[0].IsRunning);
    }

    private static CodexAssistantConnection Connection(
        List<CountingTransport> transports, bool failFirstInitialize = false)
    {
        int created = 0;
        return new CodexAssistantConnection(
            new AssistantProviderOptions(AssistantProviderOptions.CodexBackend, "", "codex-test-path"),
            (_, _) =>
            {
                var transport = new CountingTransport();
                if (failFirstInitialize && created == 0)
                    transport.Server.FailMethodOnce = CodexMethods.Initialize;
                created++;
                transports.Add(transport);
                return transport;
            });
    }

    private sealed class CountingTransport : ICodexTransport
    {
        internal CodexTestServer Server { get; } = new();
        internal int StartCount { get; private set; }
        internal int DisposeCount { get; private set; }

        public event Action<string>? LineReceived
        {
            add => Server.LineReceived += value;
            remove => Server.LineReceived -= value;
        }

        public event Action<CodexProcessExit>? Exited
        {
            add => Server.Exited += value;
            remove => Server.Exited -= value;
        }

        public bool IsRunning => Server.IsRunning;

        public async Task StartAsync(CancellationToken cancellationToken)
        {
            StartCount++;
            await Server.StartAsync(cancellationToken);
        }

        public Task SendLineAsync(string line, CancellationToken cancellationToken) =>
            Server.SendLineAsync(line, cancellationToken);

        public void Exit() => Server.Exit();

        public void Dispose()
        {
            DisposeCount++;
            Server.Dispose();
        }
    }
}
