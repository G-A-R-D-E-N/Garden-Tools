using System.Collections.Generic;
using Avalonia.Controls;

namespace BehaviourStudio.App;

public sealed record AssistantModelItem(string WireModel, string Label)
{
    public override string ToString() => Label;
}

internal static class AssistantModelSelector
{
    private static bool _binding;

    public static void Bind(
        ComboBox select, AssistantPane pane,
        IReadOnlyList<CodexModel> models, string selectedModel)
    {
        _binding = true;
        try
        {
            select.ItemsSource = null;
            if (models.Count == 0)
            {
                select.SelectedItem = null;
                select.IsVisible = false;
                pane.ModelResetVisible = false;
                return;
            }

            var items = new List<AssistantModelItem>(models.Count);
            AssistantModelItem? chosen = null;
            foreach (CodexModel model in models)
            {
                var item = new AssistantModelItem(model.WireModel, model.DisplayName);
                items.Add(item);
                if (item.WireModel == selectedModel) chosen = item;
            }

            select.ItemsSource = items;
            select.IsVisible = true;
            select.PlaceholderText = "Codex config default";
            select.SelectedItem = chosen;
            pane.ModelResetVisible = chosen is not null;
        }
        finally
        {
            _binding = false;
        }
    }

    public static void HookEvents(AssistantPane pane)
    {
        ComboBox select = pane.ModelSelector;
        Button reset = pane.ModelReset;
        select.SelectionChanged += (_, _) =>
        {
            if (_binding) return;
            if (select.SelectedItem is AssistantModelItem item)
                pane.InvokeModelSelected(item.WireModel);
        };
        reset.Click += (_, _) => pane.InvokeModelResetRequested();
    }
}
