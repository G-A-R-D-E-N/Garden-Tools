using System.Windows.Controls;
using FO4RecordEditor.Models;
using FO4RecordEditor.ViewModels;

namespace FO4RecordEditor.Views;

public static class AiActions
{
    public static ContextMenu Build(ShellViewModel shell, RecordNode record,
        Action<RecordNode> open, Action<string> submitToChat)
    {
        var menu = new ContextMenu();
        var entry = shell.Graph.GetByFormKey(record.GetValue("FormKey") ?? "");

        Add(menu, "Explain Record", () => SeedChat(shell, submitToChat,
            $"Explain this record in plain language: {Describe(entry, shell)}"));
        Add(menu, "Fix Errors", () => SeedChat(shell, submitToChat,
            $"List any errors in this record and how to fix them: {Describe(entry, shell)}"));
        Add(menu, "Generate Patch", () => SeedChat(shell, submitToChat,
            $"Propose a compatibility patch involving this record: {Describe(entry, shell)}"));
        Add(menu, "Compare to Vanilla", () => SeedChat(shell, submitToChat,
            $"Compare this record to its vanilla Fallout4.esm counterpart and summarize the changes: {Describe(entry, shell)}"));
        Add(menu, "Analyze Impact (what breaks if deleted)", () =>
        {
            if (entry == null) return;
            var report = shell.Graph.AnalyzeImpact(entry.FormKey);
            SeedChat(shell, submitToChat,
                $"If I delete {entry.EditorID} [{entry.FormKey}], these {report.AffectedRecords.Count} " +
                $"records reference it and would break: " +
                string.Join(", ", report.AffectedRecords.Take(30).Select(r => r.EditorID)) +
                ". Explain the consequences and safest fix.");
        });
        return menu;
    }

    private static void Add(ContextMenu menu, string header, Action action)
    {
        var item = new MenuItem { Header = header };
        item.Click += (_, _) => action();
        menu.Items.Add(item);
    }

    private static string Describe(RecordEntry? e, ShellViewModel shell) =>
        e == null ? "(record not indexed)" : shell.Context.BuildForRecord(e);

    private static void SeedChat(ShellViewModel shell, Action<string> submitToChat, string prompt)
    {
        shell.Log.Log(LogCategory.AI, LogLevel.Info, "AI action queued", prompt[..Math.Min(80, prompt.Length)]);
        submitToChat(prompt);
    }
}
