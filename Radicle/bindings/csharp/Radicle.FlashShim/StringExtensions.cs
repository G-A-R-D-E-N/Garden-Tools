using System;
using System.Text;
using System.Text.RegularExpressions;

namespace Flash;

public static class StringExtensions
{
    private static int Clamp(double value, int length)
    {
        if (double.IsNaN(value))
        {
            return 0;
        }
        double t = Math.Truncate(value);
        return t < 0 ? 0 : t > length ? length : (int)t;
    }

    private static int Resolve(double value, int length)
    {
        if (double.IsNaN(value))
        {
            return 0;
        }
        double t = Math.Truncate(value);
        if (t < 0)
        {
            t += length;
        }
        return t < 0 ? 0 : t > length ? length : (int)t;
    }

    public static string CharAt(this string text, double index = 0)
    {
        return index >= 0 && index < text.Length ? text[(int)index].ToString() : string.Empty;
    }

    public static double CharCodeAt(this string text, double index = 0)
    {
        return index >= 0 && index < text.Length ? text[(int)index] : double.NaN;
    }

    public static string Substr(this string text, double start = 0, double length = 2147483647)
    {
        int from = Resolve(start, text.Length);
        double count = double.IsNaN(length) ? 0 : Math.Truncate(length);
        if (count <= 0)
        {
            return string.Empty;
        }
        return text.Substring(from, (int)Math.Min(count, text.Length - from));
    }

    public static string Substring(this string text, double start = 0, double end = 2147483647)
    {
        int a = Clamp(start, text.Length);
        int b = Clamp(end, text.Length);
        return a <= b ? text.Substring(a, b - a) : text.Substring(b, a - b);
    }

    public static string Slice(this string text, double start = 0, double end = 2147483647)
    {
        int a = Resolve(start, text.Length);
        int b = Resolve(end, text.Length);
        return b > a ? text.Substring(a, b - a) : string.Empty;
    }

    public static string Concat(this string text, params object?[]? values)
    {
        StringBuilder builder = new StringBuilder(text);
        foreach (object? value in Ops.Rest(values))
        {
            builder.Append(Ops.ToString(value));
        }
        return builder.ToString();
    }

    public static string ToLowerCase(this string text) => text.ToLowerInvariant();

    public static string ToUpperCase(this string text) => text.ToUpperInvariant();

    public static int IndexOf(this string text, string value, double startIndex = 0)
    {
        return text.IndexOf(value, Clamp(startIndex, text.Length), StringComparison.Ordinal);
    }

    public static int LastIndexOf(this string text, string value, double startIndex = 2147483647)
    {
        int from = Clamp(startIndex, text.Length);
        int end = Math.Min(text.Length - value.Length, from);
        for (int i = end; i >= 0; i--)
        {
            if (string.CompareOrdinal(text, i, value, 0, value.Length) == 0)
            {
                return i;
            }
        }
        return -1;
    }

    public static AsArray Split(this string text, object? delimiter = null, double limit = 4294967295)
    {
        AsArray result = new AsArray();
        if (delimiter == null)
        {
            result.Push(text);
            return result;
        }
        string[] parts;
        if (delimiter is Regex pattern)
        {
            parts = pattern.Split(text);
        }
        else
        {
            string glue = Ops.ToString(delimiter);
            if (glue.Length == 0)
            {
                parts = new string[text.Length];
                for (int i = 0; i < parts.Length; i++)
                {
                    parts[i] = text[i].ToString();
                }
            }
            else
            {
                parts = text.Split(new[] { glue }, StringSplitOptions.None);
            }
        }
        for (int i = 0; i < parts.Length && i < limit; i++)
        {
            result.Push(parts[i]);
        }
        return result;
    }

    public static string Replace(this string text, object? pattern, object? replacement)
    {
        string substitute = Ops.ToString(replacement);
        if (pattern is Regex regex)
        {
            return regex.Replace(text, substitute.Replace("$", "$$"));
        }
        string find = Ops.ToString(pattern);
        int at = text.IndexOf(find, StringComparison.Ordinal);
        return at < 0 ? text : text.Substring(0, at) + substitute + text.Substring(at + find.Length);
    }

    private static Regex AsRegex(object? pattern) => pattern as Regex ?? new Regex(Ops.ToString(pattern));

    public static AsArray? Match(this string text, object? pattern)
    {
        var found = AsRegex(pattern).Match(text);
        if (!found.Success)
        {
            return null;
        }
        AsArray result = new AsArray();
        foreach (Group captured in found.Groups)
        {
            result.Push(captured.Success ? captured.Value : null);
        }
        return result;
    }

    public static int Search(this string text, object? pattern)
    {
        var found = AsRegex(pattern).Match(text);
        return found.Success ? found.Index : -1;
    }

    public static string ValueOf(this string text) => text;
}

public static class FunctionExtensions
{
    public static object? Call(this Delegate function, object? thisArg, params object?[]? args)
    {
        return Ops.Invoke(function, Ops.Rest(args));
    }

    public static object? Apply(this Delegate function, object? thisArg, AsArray? args = null)
    {
        return Ops.Invoke(function, args == null ? Array.Empty<object?>() : args.ToArray());
    }
}
