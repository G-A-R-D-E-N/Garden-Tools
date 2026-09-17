using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;

namespace BehaviourStudio.App;

public sealed class AssistantModelPicker : Border
{
    private const double MaximumListHeight = 320;

    private readonly TextBox _search = new() { PlaceholderText = "Search models" };
    private readonly StackPanel _rows = new() { Spacing = 1 };
    private readonly TextBlock _empty = new()
    {
        Text = "No models match that search.",
        Foreground = Ux.MutedBrush,
        FontSize = Ux.FontSmall,
        Margin = new Thickness(6, 8, 6, 8),
    };
    private readonly List<CodexModel> _visible = new();
    private IReadOnlyList<CodexModel> _models = Array.Empty<CodexModel>();
    private string _selected = "";
    private string _query = "";
    private int _highlight = -1;

    public AssistantModelPicker()
    {
        Background = Ux.CardBrush;
        BorderBrush = Ux.BorderBrush;
        BorderThickness = new Thickness(1);
        CornerRadius = new CornerRadius(Ux.Radius);
        Padding = new Thickness(8);
        MinWidth = 320;

        _search.FontSize = Ux.FontSmall;
        _search.KeyDown += OnSearchKeyDown;
        _search.TextChanged += (_, _) =>
        {
            string text = _search.Text ?? "";
            if (text == _query) return;
            _query = text;
            Rebuild();
        };

        var hint = new TextBlock
        {
            Text = "Up/Down navigate \u00B7 Enter select \u00B7 Esc close",
            Foreground = Ux.MutedBrush,
            FontSize = Ux.FontSmall,
            Margin = new Thickness(6, 6, 6, 0),
        };

        var list = new ScrollViewer
        {
            VerticalScrollBarVisibility = Avalonia.Controls.Primitives.ScrollBarVisibility.Auto,
            HorizontalScrollBarVisibility = Avalonia.Controls.Primitives.ScrollBarVisibility.Disabled,
            MaxHeight = MaximumListHeight,
            Content = _rows,
        };

        Child = new StackPanel
        {
            Spacing = 4,
            Children = { _search, list, _empty, hint },
        };
    }

    public event Action<string>? Chosen;

    public string Query => _query;
    public string SelectedModel => _selected;
    public IReadOnlyList<string> VisibleModels => _visible.Select(model => model.WireModel).ToArray();
    public int HighlightedIndex => _highlight;
    public TextBox Search => _search;

    public void SetModels(IReadOnlyList<CodexModel> models, string selected)
    {
        _models = models ?? Array.Empty<CodexModel>();
        _selected = selected ?? "";
        Rebuild();
    }

    public void SetQuery(string text)
    {
        _query = text ?? "";
        _search.Text = _query;
        Rebuild();
    }

    public void FocusSearch() => _search.Focus();

    internal void InvokeFavorite(string wireModel)
    {
        AssistantModelPrefs.ToggleFavorite(wireModel);
        Rebuild();
    }

    public bool Choose(string wireModel)
    {
        if (!_models.Any(model => string.Equals(model.WireModel, wireModel, StringComparison.Ordinal)))
            return false;
        _selected = wireModel;
        AssistantModelPrefs.Remember(wireModel);
        Rebuild();
        Chosen?.Invoke(wireModel);
        return true;
    }

    internal static string ContextLabel(long context) => context switch
    {
        >= 1_000_000 => (context / 1_000_000.0).ToString("0.#", CultureInfo.InvariantCulture) + "M",
        >= 1_000 => (context / 1_000.0).ToString("0.#", CultureInfo.InvariantCulture) + "K",
        > 0 => context.ToString(CultureInfo.InvariantCulture),
        _ => "",
    };

    private void Rebuild()
    {
        _rows.Children.Clear();
        _visible.Clear();
        _highlight = -1;

        var matching = new List<CodexModel>();
        foreach (CodexModel model in _models)
            if (Matches(model)) matching.Add(model);

        _empty.IsVisible = matching.Count == 0;

        var placed = new HashSet<string>(StringComparer.Ordinal);
        AddSection("Favorites", matching, AssistantModelPrefs.Favorites(), placed);
        AddSection("Recent", matching, AssistantModelPrefs.Recents(), placed);
        foreach (IGrouping<string, CodexModel> group in matching
                     .Where(model => !placed.Contains(model.WireModel))
                     .GroupBy(model => model.Group.Length > 0 ? model.Group : "Models"))
        {
            AddSection(Title(group.Key), group.ToList(), null, placed);
        }

        if (_highlight < 0 && _visible.Count > 0) _highlight = 0;
        PaintHighlight();
    }

    private void AddSection(
        string title, IReadOnlyList<CodexModel> matching, IReadOnlyList<string>? order, HashSet<string> placed)
    {
        List<CodexModel> rows = order is null
            ? matching.Where(model => !placed.Contains(model.WireModel)).ToList()
            : order
                .Select(id => matching.FirstOrDefault(model =>
                    string.Equals(model.WireModel, id, StringComparison.Ordinal)))
                .Where(model => model is not null)
                .Select(model => model!)
                .Where(model => !placed.Contains(model.WireModel))
                .ToList();
        if (rows.Count == 0) return;

        _rows.Children.Add(new TextBlock
        {
            Text = title.ToUpperInvariant(),
            Foreground = Ux.MutedBrush,
            FontSize = Ux.FontSmall,
            FontWeight = FontWeight.SemiBold,
            Margin = new Thickness(6, 8, 6, 2),
        });

        foreach (CodexModel model in rows)
        {
            placed.Add(model.WireModel);
            _visible.Add(model);
            _rows.Children.Add(BuildRow(model, _visible.Count - 1));
        }
    }

    private Control BuildRow(CodexModel model, int index)
    {
        var name = new TextBlock
        {
            Text = model.DisplayName,
            Foreground = Ux.TitleBrush,
            FontSize = Ux.FontSmall,
            FontWeight = model.WireModel == _selected ? FontWeight.SemiBold : FontWeight.Normal,
            TextTrimming = TextTrimming.CharacterEllipsis,
            VerticalAlignment = VerticalAlignment.Center,
        };

        var meta = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Spacing = 6,
            VerticalAlignment = VerticalAlignment.Center,
        };
        string context = ContextLabel(model.ContextLimit);
        if (context.Length > 0) meta.Children.Add(Chip(context));
        if (model.Reasoning) meta.Children.Add(Chip("Thinking"));

        var star = new Button
        {
            Content = AssistantModelPrefs.IsFavorite(model.WireModel) ? "\u2605" : "\u2606",
            Background = Brushes.Transparent,
            BorderThickness = new Thickness(0),
            Foreground = AssistantModelPrefs.IsFavorite(model.WireModel) ? Ux.WarnBrush : Ux.MutedBrush,
            FontSize = 14,
            Padding = new Thickness(6, 0, 6, 0),
        };
        ToolTip.SetTip(star, "Favourite this model");
        star.Click += (_, _) =>
        {
            AssistantModelPrefs.ToggleFavorite(model.WireModel);
            Rebuild();
        };

        var layout = new DockPanel { LastChildFill = true };
        DockPanel.SetDock(meta, Dock.Right);
        DockPanel.SetDock(star, Dock.Right);
        layout.Children.Add(star);
        layout.Children.Add(meta);
        layout.Children.Add(name);

        var row = new Button
        {
            Content = layout,
            HorizontalAlignment = HorizontalAlignment.Stretch,
            HorizontalContentAlignment = HorizontalAlignment.Stretch,
            Background = model.WireModel == _selected ? Ux.CardHoverBrush : Brushes.Transparent,
            BorderThickness = new Thickness(0),
            CornerRadius = new CornerRadius(Ux.Radius),
            Padding = new Thickness(6, 4, 6, 4),
            Tag = index,
        };
        row.Click += (_, _) => Choose(model.WireModel);
        return row;
    }

    private static TextBlock Chip(string text) => new()
    {
        Text = text,
        Foreground = Ux.CodeBrush,
        FontSize = Ux.FontSmall,
        VerticalAlignment = VerticalAlignment.Center,
    };

    private void OnSearchKeyDown(object? sender, KeyEventArgs args)
    {
        switch (args.Key)
        {
            case Key.Down:
                Move(1);
                args.Handled = true;
                break;
            case Key.Up:
                Move(-1);
                args.Handled = true;
                break;
            case Key.Enter:
                if (_highlight >= 0 && _highlight < _visible.Count) Choose(_visible[_highlight].WireModel);
                args.Handled = true;
                break;
        }
    }

    private void Move(int delta)
    {
        if (_visible.Count == 0) return;
        _highlight = (_highlight + delta + _visible.Count) % _visible.Count;
        PaintHighlight();
    }

    private void PaintHighlight()
    {
        for (int index = 0; index < _rows.Children.Count; index++)
        {
            if (_rows.Children[index] is not Button row || row.Tag is not int slot) continue;
            bool on = slot == _highlight;
            row.Background = on || _visible[slot].WireModel == _selected ? Ux.CardHoverBrush : Brushes.Transparent;
        }
    }

    private bool Matches(CodexModel model)
    {
        if (_query.Length == 0) return true;
        return model.DisplayName.Contains(_query, StringComparison.OrdinalIgnoreCase) ||
               model.WireModel.Contains(_query, StringComparison.OrdinalIgnoreCase);
    }

    private static string Title(string group) =>
        group.Length == 0 ? "Models" : char.ToUpperInvariant(group[0]) + group[1..];
}
