using System;
using System.Collections.Generic;
using System.Net;
using System.Text.RegularExpressions;
using Flash.Display;
using Flash.Events;

namespace Flash.Text;

public static class TextFieldAutoSize
{
    public const string CENTER = "center";
    public const string LEFT = "left";
    public const string NONE = "none";
    public const string RIGHT = "right";
}

public static class TextFieldType
{
    public const string DYNAMIC = "dynamic";
    public const string INPUT = "input";
}

public static class TextFormatAlign
{
    public const string CENTER = "center";
    public const string END = "end";
    public const string JUSTIFY = "justify";
    public const string LEFT = "left";
    public const string RIGHT = "right";
    public const string START = "start";
}

public static class AntiAliasType
{
    public const string ADVANCED = "advanced";
    public const string NORMAL = "normal";
}

public static class GridFitType
{
    public const string NONE = "none";
    public const string PIXEL = "pixel";
    public const string SUBPIXEL = "subpixel";
}

public class TextFormat
{
    public TextFormat(string? font = null, object? size = null, object? color = null, object? bold = null,
                      object? italic = null, object? underline = null, string? url = null, string? target = null,
                      string? align = null, object? leftMargin = null, object? rightMargin = null,
                      object? indent = null, object? leading = null)
    {
        Font = font;
        Size = size;
        Color = color;
        Bold = bold;
        Italic = italic;
        Underline = underline;
        Url = url;
        Target = target;
        Align = align;
        LeftMargin = leftMargin;
        RightMargin = rightMargin;
        Indent = indent;
        Leading = leading;
    }

    public string? Font { get; set; }

    public object? Size { get; set; }

    public object? Color { get; set; }

    public object? Bold { get; set; }

    public object? Italic { get; set; }

    public object? Underline { get; set; }

    public string? Url { get; set; }

    public string? Target { get; set; }

    public string? Align { get; set; }

    public object? LeftMargin { get; set; }

    public object? RightMargin { get; set; }

    public object? Indent { get; set; }

    public object? Leading { get; set; }

    public object? LetterSpacing { get; set; }

    public TextFormat Clone()
    {
        TextFormat copy = new TextFormat();
        copy.Merge(this);
        return copy;
    }

    internal void Merge(TextFormat other)
    {
        Font = other.Font ?? Font;
        Size = other.Size ?? Size;
        Color = other.Color ?? Color;
        Bold = other.Bold ?? Bold;
        Italic = other.Italic ?? Italic;
        Underline = other.Underline ?? Underline;
        Url = other.Url ?? Url;
        Target = other.Target ?? Target;
        Align = other.Align ?? Align;
        LeftMargin = other.LeftMargin ?? LeftMargin;
        RightMargin = other.RightMargin ?? RightMargin;
        Indent = other.Indent ?? Indent;
        Leading = other.Leading ?? Leading;
        LetterSpacing = other.LetterSpacing ?? LetterSpacing;
    }
}

public class TextLineMetrics
{
    public TextLineMetrics(double x = 0, double width = 0, double height = 0, double ascent = 0, double descent = 0,
                           double leading = 0)
    {
        X = x;
        Width = width;
        Height = height;
        Ascent = ascent;
        Descent = descent;
        Leading = leading;
    }

    public double X { get; set; }

    public double Width { get; set; }

    public double Height { get; set; }

    public double Ascent { get; set; }

    public double Descent { get; set; }

    public double Leading { get; set; }
}

public class StyleSheet : EventDispatcher
{
    private readonly AsObject _styles = new AsObject();

    public AsArray StyleNames
    {
        get
        {
            AsArray names = new AsArray();
            foreach (string key in _styles.Keys)
            {
                names.Push(key);
            }
            return names;
        }
    }

    public void SetStyle(string styleName, AsObject style) => _styles[styleName] = style;

    public AsObject? GetStyle(string styleName) => _styles[styleName] as AsObject;

    public void Clear()
    {
        foreach (string key in new List<string>(_styles.Keys))
        {
            _styles.Remove(key);
        }
    }
}

public class TextField : InteractiveObject
{
    private static readonly Regex LineBreak = new Regex(@"<br\s*/?>", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    private static readonly Regex Tag = new Regex(@"<[^>]*>", RegexOptions.CultureInvariant);

    private string _text = string.Empty;
    private string? _html;

    public string Text
    {
        get => _text;
        set
        {
            _text = value ?? string.Empty;
            _html = null;
        }
    }

    public string HtmlText
    {
        get => _html ?? WebUtility.HtmlEncode(_text);
        set
        {
            _html = value ?? string.Empty;
            _text = WebUtility.HtmlDecode(Tag.Replace(LineBreak.Replace(_html, "\r"), string.Empty));
        }
    }

    public uint TextColor { get; set; }

    public double TextWidth { get; set; }

    public double TextHeight { get; set; }

    public bool Multiline { get; set; }

    public bool WordWrap { get; set; }

    public bool Selectable { get; set; } = true;

    public string AutoSize { get; set; } = "none";

    public string Type { get; set; } = "dynamic";

    public string AntiAliasType { get; set; } = "normal";

    public bool EmbedFonts { get; set; }

    public int MaxChars { get; set; }

    public int ScrollV { get; set; } = 1;

    public int MaxScrollV { get; set; } = 1;

    public string VerticalAlign { get; set; } = "none";

    public string VerticalAutoSize { get; set; } = "none";

    public object? ImageSubstitutions { get; set; }

    public int Length => _text.Length;

    public int NumLines
    {
        get
        {
            int lines = 1;
            for (int i = 0; i < _text.Length; i++)
            {
                if (_text[i] == '\r' || _text[i] == '\n')
                {
                    lines++;
                    if (_text[i] == '\r' && i + 1 < _text.Length && _text[i + 1] == '\n')
                    {
                        i++;
                    }
                }
            }
            return lines;
        }
    }

    public TextFormat DefaultTextFormat { get; set; } = new TextFormat();

    public void SetTextFormat(TextFormat format, int beginIndex = -1, int endIndex = -1) =>
        DefaultTextFormat.Merge(format);

    public TextFormat GetTextFormat(int beginIndex = -1, int endIndex = -1) => DefaultTextFormat.Clone();

    public void AppendText(string newText)
    {
        _text += newText;
        if (_html != null)
        {
            _html += WebUtility.HtmlEncode(newText);
        }
    }

    public void ReplaceText(int beginIndex, int endIndex, string newText)
    {
        int begin = Math.Max(0, Math.Min(beginIndex, _text.Length));
        int end = Math.Max(begin, Math.Min(endIndex, _text.Length));
        _text = _text.Substring(0, begin) + newText + _text.Substring(end);
        _html = null;
    }
}
