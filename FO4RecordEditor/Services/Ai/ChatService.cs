using FO4RecordEditor.Models;

namespace FO4RecordEditor.Services;

public sealed class ChatService
{
    private readonly List<ChatMessage> _history = new();
    private IAIProvider _provider;

    public ChatService(IAIProvider provider) => _provider = provider;
    public void SetProvider(IAIProvider provider) => _provider = provider;
    public IReadOnlyList<ChatMessage> History => _history;
    public void Reset() => _history.Clear();

    public async Task<string> SendAsync(
        string userMessage, string? systemContext,
        Action<string> onToken, CancellationToken ct = default)
    {
        var outgoing = new List<ChatMessage>();
        if (systemContext != null) outgoing.Add(new ChatMessage(ChatRole.System, systemContext));
        outgoing.AddRange(_history);
        outgoing.Add(new ChatMessage(ChatRole.User, userMessage));

        _history.Add(new ChatMessage(ChatRole.User, userMessage));

        var full = new System.Text.StringBuilder();
        await foreach (var token in _provider.StreamAsync(outgoing, ct))
        {
            full.Append(token);
            onToken(token);
        }
        _history.Add(new ChatMessage(ChatRole.Assistant, full.ToString()));
        return full.ToString();
    }
}
