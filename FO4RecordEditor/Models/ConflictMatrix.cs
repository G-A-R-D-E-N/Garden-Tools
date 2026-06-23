using System.Collections.Generic;

namespace FO4RecordEditor.Models;

/// <summary>
/// A field-level, xEdit-style view of one conflicting record: every plugin that touches the
/// record becomes a column, every field a row. <see cref="ConflictFieldRow.Differs"/> marks the
/// rows where the plugins actually disagree (what truly conflicts).
/// </summary>
public sealed class ConflictMatrix
{
    public required string FormKey { get; init; }
    public required string EditorID { get; init; }
    public required string Type { get; init; }
    /// <summary>Plugins in load order; the last one is the current winner.</summary>
    public required IReadOnlyList<string> Plugins { get; init; }
    public required string Winner { get; init; }
    public required IReadOnlyList<ConflictFieldRow> Rows { get; init; }
}

public sealed class ConflictFieldRow
{
    public required string Field { get; init; }
    public required string DisplayLabel { get; init; }
    public required int Level { get; init; }
    public System.Windows.Thickness Spacer => new System.Windows.Thickness(Level * 16, 0, 0, 0);

    /// <summary>Value per plugin, parallel to <see cref="ConflictMatrix.Plugins"/>. "" = not set.</summary>
    public required IReadOnlyList<string> Values { get; init; }
    public required bool Differs { get; init; }

    /// <summary>This row has a one-line summary (e.g. a condition or component) AND deeper sub-field
    /// rows beneath it — the grid collapses those children by default and expands them on click,
    /// like double-clicking an array element in xEdit.</summary>
    public bool IsSummary { get; init; }
    /// <summary>True if any deeper rows belong to this one (so the grid shows an expand chevron).</summary>
    public bool HasChildren { get; init; }

    /// <summary>How a value cell edits: "Text", "Bool" (checkbox), or "Enum" (dropdown).</summary>
    public string EditKind { get; init; } = "Text";
    /// <summary>Dropdown options when EditKind == "Enum".</summary>
    public IReadOnlyList<string>? EnumOptions { get; init; }

    /// <summary>Short display label for the picker chip when EditKind == "Ref" (e.g. "Keyword").</summary>
    public string? RefType { get; init; }
    /// <summary>Comma-separated concrete record types the link may target (the picker's filter set).</summary>
    public string? RefTypes { get; init; }

    // Bindable accessors for up to 8 plugin columns (DataGrid columns are generated to match).
    public string V0 => Get(0); public string V1 => Get(1); public string V2 => Get(2); public string V3 => Get(3);
    public string V4 => Get(4); public string V5 => Get(5); public string V6 => Get(6); public string V7 => Get(7);
    private string Get(int i) => i < Values.Count ? Values[i] : "";
}
