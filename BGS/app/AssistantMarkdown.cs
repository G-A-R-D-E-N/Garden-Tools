using System;
using System.Collections.Generic;
using System.Text;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Documents;
using Avalonia.Media;

namespace BehaviourStudio.App;

public enum MarkdownBlockKind
{
    Paragraph,
    Heading,
    Bullet,
    Numbered,
    Code,
    Rule,
}

public sealed record MarkdownSpan(string Text, bool Bold, bool Italic, bool Code);

public sealed record MarkdownBlock(
    MarkdownBlockKind Kind, IReadOnlyList<MarkdownSpan> Spans, int Level = 0, string Marker = "");

public static class AssistantMarkdown
{
    private static readonly FontFamily Mono =
        new("Cascadia Mono,Consolas,Menlo,DejaVu Sans Mono,monospace");

    public static IReadOnlyList<MarkdownBlock> Parse(string text)
    {
        var blocks = new List<MarkdownBlock>();
        if (string.IsNullOrEmpty(text)) return blocks;

        string[] lines = text.Replace("\r\n", "\n").Replace('\r', '\n').Split('\n');
        var paragraph = new List<string>();
        var code = new List<string>();
        string language = "";
        bool fenced = false;

        void FlushParagraph()
        {
            if (paragraph.Count == 0) return;
            blocks.Add(new MarkdownBlock(MarkdownBlockKind.Paragraph,
                Inline(string.Join(" ", paragraph))));
            paragraph.Clear();
        }

        void FlushCode()
        {
            blocks.Add(new MarkdownBlock(MarkdownBlockKind.Code,
                new[] { new MarkdownSpan(string.Join("\n", code), false, false, true) },
                0, language));
            code.Clear();
            language = "";
        }

        foreach (string raw in lines)
        {
            string line = raw.TrimEnd();
            string trimmed = line.TrimStart();

            if (fenced)
            {
                if (IsFence(trimmed))
                {
                    fenced = false;
                    FlushCode();
                }
                else
                {
                    code.Add(line);
                }
                continue;
            }

            if (IsFence(trimmed))
            {
                FlushParagraph();
                fenced = true;
                language = trimmed.Trim('`', '~').Trim();
                continue;
            }

            if (trimmed.Length == 0)
            {
                FlushParagraph();
                continue;
            }

            if (IsRule(trimmed))
            {
                FlushParagraph();
                blocks.Add(new MarkdownBlock(MarkdownBlockKind.Rule, Array.Empty<MarkdownSpan>()));
                continue;
            }

            int level = HeadingLevel(trimmed);
            if (level > 0)
            {
                FlushParagraph();
                blocks.Add(new MarkdownBlock(MarkdownBlockKind.Heading,
                    Inline(trimmed[(level + 1)..].Trim()), level));
                continue;
            }

            if (TryBullet(trimmed, out string bullet))
            {
                FlushParagraph();
                blocks.Add(new MarkdownBlock(MarkdownBlockKind.Bullet, Inline(bullet), 0, "\u2022"));
                continue;
            }

            if (TryNumbered(trimmed, out string marker, out string body))
            {
                FlushParagraph();
                blocks.Add(new MarkdownBlock(MarkdownBlockKind.Numbered, Inline(body), 0, marker));
                continue;
            }

            paragraph.Add(trimmed);
        }

        if (fenced) FlushCode();
        FlushParagraph();
        return blocks;
    }

    public static IReadOnlyList<MarkdownSpan> Inline(string text)
    {
        var spans = new List<MarkdownSpan>();
        if (string.IsNullOrEmpty(text)) return spans;
        var buffer = new StringBuilder();
        int index = 0;

        void Flush()
        {
            if (buffer.Length == 0) return;
            spans.Add(new MarkdownSpan(buffer.ToString(), false, false, false));
            buffer.Clear();
        }

        while (index < text.Length)
        {
            char current = text[index];
            if (current == '`' && text.IndexOf('`', index + 1) is int tick && tick > index)
            {
                Flush();
                spans.Add(new MarkdownSpan(text[(index + 1)..tick], false, false, true));
                index = tick + 1;
                continue;
            }
            if (current == '*' && index + 1 < text.Length && text[index + 1] == '*' &&
                text.IndexOf("**", index + 2, StringComparison.Ordinal) is int bold && bold > index)
            {
                Flush();
                spans.Add(new MarkdownSpan(text[(index + 2)..bold], true, false, false));
                index = bold + 2;
                continue;
            }
            if (current == '*' && text.IndexOf('*', index + 1) is int star && star > index + 1)
            {
                Flush();
                spans.Add(new MarkdownSpan(text[(index + 1)..star], false, true, false));
                index = star + 1;
                continue;
            }
            if (current == '_' && IsEmphasisUnderscore(text, index))
            {
                int close = text.IndexOf('_', index + 1);
                Flush();
                spans.Add(new MarkdownSpan(text[(index + 1)..close], false, true, false));
                index = close + 1;
                continue;
            }
            if (current == '[' && text.IndexOf(']', index + 1) is int label && label > index &&
                label + 1 < text.Length && text[label + 1] == '(' &&
                text.IndexOf(')', label + 2) is int target && target > label)
            {
                Flush();
                spans.Add(new MarkdownSpan(text[(index + 1)..label], false, false, false));
                index = target + 1;
                continue;
            }
            buffer.Append(current);
            index++;
        }

        Flush();
        return spans;
    }

    public static Control Render(string text)
    {
        var panel = new StackPanel { Spacing = 6 };
        foreach (MarkdownBlock block in Parse(text)) panel.Children.Add(RenderBlock(block));
        return panel;
    }

    private static Control RenderBlock(MarkdownBlock block) => block.Kind switch
    {
        MarkdownBlockKind.Code => new Border
        {
            Background = Ux.RailBrush,
            BorderBrush = Ux.BorderBrush,
            BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(Ux.Radius),
            Padding = new Thickness(8),
            Child = new TextBlock
            {
                Text = block.Spans.Count > 0 ? block.Spans[0].Text : "",
                FontFamily = Mono,
                FontSize = Ux.FontSmall,
                Foreground = Ux.CodeBrush,
                TextWrapping = TextWrapping.NoWrap,
            },
        },
        MarkdownBlockKind.Rule => new Border
        {
            Height = 1,
            Background = Ux.BorderBrush,
            Margin = new Thickness(0, 4, 0, 4),
        },
        MarkdownBlockKind.Heading => Paragraph(block.Spans, Ux.TitleBrush, HeadingSize(block.Level),
            FontWeight.SemiBold),
        MarkdownBlockKind.Bullet or MarkdownBlockKind.Numbered => Indented(block.Marker, block.Spans),
        _ => Paragraph(block.Spans, Ux.MetaBrush, Ux.FontBody, FontWeight.Normal),
    };

    private static Control Paragraph(
        IReadOnlyList<MarkdownSpan> spans, IBrush brush, double size, FontWeight weight)
    {
        var text = new TextBlock
        {
            Foreground = brush,
            FontSize = size,
            FontWeight = weight,
            TextWrapping = TextWrapping.Wrap,
        };
        AddRuns(text.Inlines!, spans);
        return text;
    }

    private static Control Indented(string marker, IReadOnlyList<MarkdownSpan> spans)
    {
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("18,*") };
        var bullet = new TextBlock
        {
            Text = marker,
            Foreground = Ux.MutedBrush,
            FontSize = Ux.FontBody,
            Margin = new Thickness(0, 0, 6, 0),
        };
        var body = Paragraph(spans, Ux.MetaBrush, Ux.FontBody, FontWeight.Normal);
        Grid.SetColumn(bullet, 0);
        Grid.SetColumn(body, 1);
        grid.Children.Add(bullet);
        grid.Children.Add(body);
        return grid;
    }

    private static void AddRuns(InlineCollection collection, IReadOnlyList<MarkdownSpan> spans)
    {
        foreach (MarkdownSpan span in spans)
        {
            var run = new Run(span.Text);
            if (span.Bold) run.FontWeight = FontWeight.SemiBold;
            if (span.Italic) run.FontStyle = FontStyle.Italic;
            if (span.Code)
            {
                run.FontFamily = Mono;
                run.Foreground = Ux.CodeBrush;
            }
            collection.Add(run);
        }
    }

    private static double HeadingSize(int level) => level switch
    {
        1 => 17,
        2 => 15,
        3 => 14,
        _ => 13,
    };

    private static bool IsEmphasisUnderscore(string text, int index)
    {
        if (index > 0 && char.IsLetterOrDigit(text[index - 1])) return false;
        if (index + 1 >= text.Length || char.IsWhiteSpace(text[index + 1])) return false;
        int close = text.IndexOf('_', index + 1);
        if (close <= index + 1) return false;
        return close == text.Length - 1 || !char.IsLetterOrDigit(text[close + 1]);
    }

    private static bool IsFence(string line) =>        line.StartsWith("```", StringComparison.Ordinal) || line.StartsWith("~~~", StringComparison.Ordinal);

    private static bool IsRule(string line)
    {
        if (line.Length < 3) return false;
        char marker = line[0];
        if (marker != '-' && marker != '*' && marker != '_') return false;
        foreach (char character in line)
            if (character != marker && character != ' ') return false;
        return true;
    }

    private static int HeadingLevel(string line)
    {
        int level = 0;
        while (level < line.Length && level < 6 && line[level] == '#') level++;
        return level > 0 && level < line.Length && line[level] == ' ' ? level : 0;
    }

    private static bool TryBullet(string line, out string body)
    {
        body = "";
        if (line.Length < 2) return false;
        if ((line[0] == '-' || line[0] == '*' || line[0] == '+') && line[1] == ' ')
        {
            body = line[2..].Trim();
            return true;
        }
        return false;
    }

    private static bool TryNumbered(string line, out string marker, out string body)
    {
        marker = "";
        body = "";
        int digits = 0;
        while (digits < line.Length && char.IsAsciiDigit(line[digits])) digits++;
        if (digits == 0 || digits + 1 >= line.Length) return false;
        if (line[digits] != '.' || line[digits + 1] != ' ') return false;
        marker = line[..(digits + 1)];
        body = line[(digits + 2)..].Trim();
        return true;
    }
}
