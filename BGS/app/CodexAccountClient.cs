using System;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace BehaviourStudio.App;

public sealed record CodexAccount(string AuthMode, string PlanType, bool SignedIn)
{
    public static readonly CodexAccount SignedOut = new("", "", false);

    public bool IsChatGpt => string.Equals(AuthMode, "chatgpt", StringComparison.Ordinal);
}

public sealed record CodexLoginChallenge(
    string LoginId,
    string AuthUrl,
    string UserCode,
    string VerificationUrl,
    bool DeviceCode)
{
    public bool HasBrowserUrl => AuthUrl.Length > 0;
}

public sealed record CodexModel(string Id, string DisplayName, bool IsDefault);

public sealed class CodexAccountClient
{
    private readonly CodexAppServerClient _client;

    public CodexAccountClient(CodexAppServerClient client) =>
        _client = client ?? throw new ArgumentNullException(nameof(client));

    public async Task<CodexAccount> ReadAccountAsync(
        bool refreshToken = false, CancellationToken cancellationToken = default)
    {
        JsonElement? result = await _client
            .RequestAsync(CodexMethods.AccountRead, new { refreshToken }, cancellationToken)
            .ConfigureAwait(false);
        JsonElement? account = CodexProtocol.Property(result, "account");
        if (account is not { } value || value.ValueKind != JsonValueKind.Object)
            return CodexAccount.SignedOut;

        string authMode = CodexProtocol.String(account, "type");
        return new CodexAccount(
            authMode,
            CodexProtocol.Scrub(CodexProtocol.String(account, "planType"), 32),
            authMode.Length > 0);
    }

    public Task<CodexLoginChallenge> StartChatGptLoginAsync(CancellationToken cancellationToken = default) =>
        StartLoginAsync(new { type = "chatgpt", appBrand = "chatgpt" }, false, cancellationToken);

    public Task<CodexLoginChallenge> StartDeviceCodeLoginAsync(CancellationToken cancellationToken = default) =>
        StartLoginAsync(new { type = "chatgptDeviceCode" }, true, cancellationToken);

    public async Task<bool> CancelLoginAsync(string loginId, CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrWhiteSpace(loginId)) return false;
        JsonElement? result = await _client
            .RequestAsync(CodexMethods.AccountLoginCancel, new { loginId }, cancellationToken)
            .ConfigureAwait(false);
        return string.Equals(CodexProtocol.String(result, "status"), "canceled", StringComparison.Ordinal);
    }

    public Task LogoutAsync(CancellationToken cancellationToken = default) =>
        _client.RequestAsync(CodexMethods.AccountLogout, new { }, cancellationToken);

    public async Task<CodexModel?> ReadDefaultModelAsync(CancellationToken cancellationToken = default)
    {
        JsonElement? result = await _client
            .RequestAsync(CodexMethods.ModelList, new { }, cancellationToken)
            .ConfigureAwait(false);
        JsonElement? data = CodexProtocol.Property(result, "data");
        if (data is not { } list || list.ValueKind != JsonValueKind.Array) return null;

        CodexModel? first = null;
        foreach (JsonElement entry in list.EnumerateArray())
        {
            if (entry.ValueKind != JsonValueKind.Object) continue;
            string id = CodexProtocol.String(entry, "id");
            if (id.Length == 0) continue;
            string displayName = CodexProtocol.String(entry, "displayName");
            bool isDefault = CodexProtocol.Bool(entry, "isDefault");
            bool hidden = CodexProtocol.Bool(entry, "hidden");
            var model = new CodexModel(id, displayName.Length > 0 ? displayName : id, isDefault);
            if (isDefault) return model;
            if (!hidden && first is null) first = model;
        }
        return first;
    }

    private async Task<CodexLoginChallenge> StartLoginAsync(
        object parameters, bool deviceCode, CancellationToken cancellationToken)
    {
        JsonElement? result = await _client
            .RequestAsync(CodexMethods.AccountLoginStart, parameters, cancellationToken)
            .ConfigureAwait(false);
        return new CodexLoginChallenge(
            CodexProtocol.String(result, "loginId"),
            CodexProtocol.String(result, "authUrl"),
            CodexProtocol.Scrub(CodexProtocol.String(result, "userCode"), 32),
            CodexProtocol.String(result, "verificationUrl"),
            deviceCode);
    }
}
