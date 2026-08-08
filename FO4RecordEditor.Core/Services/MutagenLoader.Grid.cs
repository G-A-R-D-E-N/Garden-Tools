using System.Collections;
using System.Text.Json;

namespace FO4RecordEditor.Services;

/// <summary>
/// xEdit parity: spreadsheet bulk editors (#51), GUI half. The backend generic reflection-driven
/// column model already existed for the AI-facing text table (QueryRecordSummaries); what was
/// actually missing was a structured (JSON, untruncated) version a React grid could render and a
/// human could edit, and the grid component itself (SpreadsheetPanel.tsx).
/// </summary>
public static partial class MutagenLoader
{
    /// <summary>
    /// Structured (not padded-text) version of QueryRecordSummaries for the web GUI's spreadsheet
    /// panel: { columns: string[], rows: [{ formKey, editorId, cells: string[] }], total, offset }.
    /// Reuses the SAME column-discovery logic (DiscoverGridColumns), just with more columns allowed
    /// (12 vs the AI table's 8) since there's no ~8KB text budget to protect here, and full
    /// (untruncated) cell text since a human editing a table wants the real value, not "...".
    /// </summary>
    public static string GetRecordsGridJson(object? envObj, string plugin, string sig, int limit, int offset = 0)
    {
        var mod = ResolveMod(plugin, envObj);
        if (mod == null) return JsonSerializer.Serialize(new { error = $"Plugin '{plugin}' not found." });
        var idx = GetModIndex(mod, plugin);
        var recs = RecordsOfSig(idx, sig);
        if (recs.Count == 0)
            return JsonSerializer.Serialize(new { columns = Array.Empty<string>(), rows = Array.Empty<object>(), total = 0, offset });

        var take = recs.Skip(Math.Max(0, offset)).Take(Math.Max(1, limit)).ToList();
        if (take.Count == 0)
            return JsonSerializer.Serialize(new { columns = Array.Empty<string>(), rows = Array.Empty<object>(), total = recs.Count, offset });

        var colProps = DiscoverGridColumns(take[0], 12);
        var columns = colProps.Select(p => p.Name).ToArray();

        var rows = take.Select(rec =>
        {
            var cells = colProps.Select(prop =>
            {
                try
                {
                    var v = prop.GetValue(rec);
                    return v switch
                    {
                        null => "",
                        Mutagen.Bethesda.Plugins.IFormLinkIdentifier fli => FormatFormLink(fli),
                        IEnumerable ie when v.GetType() != typeof(string) => $"[{SumCountEnumerable(ie)} items]",
                        _ => v.ToString() ?? "",
                    };
                }
                catch { return "?"; }
            }).ToArray();
            return new { formKey = rec.FormKey.ToString(), editorId = rec.EditorID ?? "", cells };
        }).ToArray();

        return JsonSerializer.Serialize(new { columns, rows, total = recs.Count, offset });
    }
}
