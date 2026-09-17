using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace BehaviourStudio.App;

public sealed record CodexAccount(string AuthMode, string PlanType, bool SignedIn)
{
    public static readonly CodexAccount SignedOut = new("", "", false);

    public bool IsChatGpt => string.Equals(AuthMode, "chatgpt", StringComparison.OrdinalIgnoreCase);
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

public sealed record CodexModel(string Id, string DisplayName, bool IsDefault, string Model = "")
{
    public string WireModel => Model.Length > 0 ? Model : Id;
    public override string ToString() => DisplayName;
}

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
        bool chatGpt = string.Equals(authMode, "chatgpt", StringComparison.OrdinalIgnoreCase);
        return new CodexAccount(
            authMode,
            chatGpt ? CodexProtocol.Scrub(CodexProtocol.String(account, "planType"), 32) : "",
            chatGpt);
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

    public async Task<IReadOnlyList<CodexModel>> ReadModelsAsync(CancellationToken cancellationToken = default)
    {
        var models = new List<CodexModel>();
        var slugs = new HashSet<string>(StringComparer.Ordinal);
        var cursors = new HashSet<string>(StringComparer.Ordinal);
        string? cursor = null;
        do
        {
            JsonElement? result = await _client.RequestAsync(CodexMethods.ModelList,
                new { cursor, limit = 100, includeHidden = false }, cancellationToken).ConfigureAwait(false);
            JsonElement? data = CodexProtocol.Property(result, "data");
            if (data is not { ValueKind: JsonValueKind.Array } list)
                throw new CodexProtocolException("Codex returned an invalid model catalog.");
            foreach (JsonElement entry in list.EnumerateArray())
            {
                if (entry.ValueKind != JsonValueKind.Object || CodexProtocol.Bool(entry, "hidden")) continue;
                string id = CodexProtocol.String(entry, "id");
                string slug = CodexProtocol.String(entry, "model");
                if (slug.Length == 0) slug = id;
                if (string.IsNullOrWhiteSpace(slug) || slug.Any(char.IsControl) || !slugs.Add(slug)) continue;
                string displayName = CodexProtocol.Scrub(CodexProtocol.String(entry, "displayName"), 120);
                models.Add(new CodexModel(id, displayName.Length > 0 ? displayName : slug,
                    CodexProtocol.Bool(entry, "isDefault"), slug));
            }
            cursor = CodexProtocol.String(result, "nextCursor");
            if (cursor.Length > 0 && (!cursors.Add(cursor) || cursors.Count >= 100))
                throw new CodexProtocolException("Codex model pagination did not finish.");
        } while (cursor.Length > 0);
        return models.AsReadOnly();
    }

    public async Task<CodexModel?> ReadDefaultModelAsync(CancellationToken cancellationToken = default)
    {
        IReadOnlyList<CodexModel> models = await ReadModelsAsync(cancellationToken).ConfigureAwait(false);
        return models.FirstOrDefault(model => model.IsDefault) ?? models.FirstOrDefault();
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
