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
    
    public void LoadHistory(IEnumerable<ChatMessage> history)
    {
        _history.Clear();
        _history.AddRange(history);
    }

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
        try
        {
            await foreach (var token in _provider.StreamAsync(outgoing, ct))
            {
                full.Append(token);
                onToken(token);
            }
        }
        finally
        {
            // Always commit an assistant turn (even on cancel/throw with partial text)
            // so the user/assistant role alternation the Messages API requires is preserved.
            // Cancellation is the normal "stop generating" path, not an edge case.
            _history.Add(new ChatMessage(ChatRole.Assistant, full.ToString()));
        }
        return full.ToString();
    }
}
