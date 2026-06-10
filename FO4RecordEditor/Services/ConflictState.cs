using FO4RecordEditor.Models;
using Mutagen.Bethesda.Plugins;

namespace FO4RecordEditor.Services;

/// <summary>
/// Holds the winning plugin per conflicting FormKey from the last conflict scan, so the
/// Explorer tree can colour records by conflict status (winner = orange, loser = red).
/// </summary>
public static class ConflictState
{
    private static Dictionary<FormKey, string> _winners = new();

    public static bool HasData => _winners.Count > 0;

    public static void Set(IEnumerable<ConflictEntry> conflicts)
    {
        var d = new Dictionary<FormKey, string>();
        foreach (var c in conflicts)
            if (FormKey.TryFactory(c.FormKey, out var fk))
                d[fk] = c.Winner;
        _winners = d;
    }

    public static void Clear() => _winners = new();

    public static ConflictStatus GetStatus(string plugin, FormKey fk)
    {
        if (!_winners.TryGetValue(fk, out var winner)) return ConflictStatus.None;
        return string.Equals(plugin, winner, StringComparison.OrdinalIgnoreCase)
            ? ConflictStatus.ConflictWinner
            : ConflictStatus.ConflictLoser;
    }

    public static ConflictStatus GetStatus(string plugin, string formKeyStr) =>
        FormKey.TryFactory(formKeyStr, out var fk) ? GetStatus(plugin, fk) : ConflictStatus.None;
}
