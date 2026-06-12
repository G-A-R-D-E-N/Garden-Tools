namespace FO4RecordEditor.Services.Rendering;

/// <summary>Relabels raw Mutagen property names to human-friendly column labels. Unknown names
/// and list indices ("[0]") pass through unchanged so nothing is ever hidden by accident.</summary>
public static class FriendlyNames
{
    private static readonly Dictionary<string, string> _map = new(StringComparer.Ordinal)
    {
        ["EditorID"] = "Editor ID",
        ["FormKey"] = "Form Key",
        ["CreatedObject"] = "Created Object",
        ["CreatedObjectCount"] = "Created Object Count",
        ["WorkbenchKeyword"] = "Workbench Keyword",
        ["PickUpSound"] = "Pick-Up Sound",
        ["PutDownSound"] = "Put-Down Sound",
        ["MarkerColor"] = "Marker Color",
        ["InteractionKeyword"] = "Interaction Keyword",
    };

    public static string Label(string raw) =>
        _map.TryGetValue(raw, out var v) ? v : raw;

    /// <summary>Relabel only a plain property name; leave dotted paths and list indices
    /// ("Components[0].Component", "[0]") structurally intact so edit paths still resolve.</summary>
    public static string LabelPath(string path)
    {
        if (path.Contains('.') || path.Contains('[')) return path;
        return Label(path);
    }
}
