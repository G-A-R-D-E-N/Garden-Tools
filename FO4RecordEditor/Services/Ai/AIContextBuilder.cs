using System.Text;
using FO4RecordEditor.Models;

namespace FO4RecordEditor.Services;

public sealed class AIContextBuilder
{
    private readonly KnowledgeGraph _graph;
    public AIContextBuilder(KnowledgeGraph graph) => _graph = graph;

    public const string SystemPreamble =
        "You are an expert Bethesda (Fallout 4) plugin modding assistant embedded in a " +
        "record editor. You understand ESP/ESM/ESL records, FormKeys (format ID:Plugin.esp), " +
        "keywords, leveled lists, OMODs, conditions (CTDA), and crafting (COBJ). When asked to " +
        "modify records, respond with a concrete change plan the user can approve. Be concise.";

    // Cap on field lines emitted per record so a wide record (e.g. a leveled list with
    // hundreds of entries) cannot balloon the prompt. Depth is already capped at 3.
    private const int MaxFieldLines = 200;

    public string BuildForRecord(RecordEntry rec)
    {
        var sb = new StringBuilder();
        sb.AppendLine(SystemPreamble);
        sb.AppendLine();
        AppendRecordContext(sb, rec);
        return sb.ToString();
    }

    public string BuildForQuestion(string question, RecordEntry? selected)
    {
        var sb = new StringBuilder();
        sb.AppendLine(SystemPreamble);   // preamble emitted exactly once
        if (selected != null)
        {
            sb.AppendLine();
            AppendRecordContext(sb, selected);
        }
        sb.AppendLine($"\n## Graph summary: {_graph.RecordCount} records, {_graph.ReferenceCount} references.");
        return sb.ToString();
    }

    // Writes the record header, fields, neighborhood, and inbound warning WITHOUT the
    // preamble, so callers control how many times the preamble appears.
    private void AppendRecordContext(StringBuilder sb, RecordEntry rec)
    {
        sb.AppendLine($"## Selected record: {rec.EditorID} ({rec.Type}) [{rec.FormKey}] in {rec.SourcePlugin}");
        int lines = 0;
        AppendFields(sb, rec.Node, 0, maxDepth: 3, ref lines);

        var hood = _graph.GetNeighborhood(rec.FormKey).Where(e => e.FormKey != rec.FormKey).ToList();
        if (hood.Count > 0)
        {
            sb.AppendLine();
            sb.AppendLine("## Linked records (one hop):");
            foreach (var n in hood.Take(20))
                sb.AppendLine($"- {n.EditorID} ({n.Type}) [{n.FormKey}]");
        }

        var inbound = _graph.GetReferencesTo(rec.FormKey);
        if (inbound.Count > 0)
            sb.AppendLine($"\n## {inbound.Count} record(s) reference this one (deleting it would break them).");
    }

    private static void AppendFields(StringBuilder sb, RecordNode node, int depth, int maxDepth, ref int lines)
    {
        if (depth > maxDepth) return;
        foreach (var c in node.Children)
        {
            if (lines >= MaxFieldLines) { sb.AppendLine("  ... (fields truncated)"); return; }
            lines++;
            var indent = new string(' ', depth * 2);
            if (c.IsLeaf) sb.AppendLine($"{indent}{c.Key}: {c.Value}");
            else { sb.AppendLine($"{indent}{c.Key}:"); AppendFields(sb, c, depth + 1, maxDepth, ref lines); }
        }
    }
}
