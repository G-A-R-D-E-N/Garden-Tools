using System;
using System.Globalization;
using System.Text.RegularExpressions;

namespace Flash;

public static class Globals
{
    private static readonly Regex FloatPrefix =
        new Regex(@"^([+-]?)(Infinity|([0-9]+\.?[0-9]*|\.[0-9]+)([eE][+-]?[0-9]+)?)", RegexOptions.CultureInvariant);

    public static Action<string>? TraceSink { get; set; }

    public static void Trace(params object?[]? args)
    {
        object?[] items = Ops.Rest(args);
        string[] parts = new string[items.Length];
        for (int i = 0; i < parts.Length; i++)
        {
            parts[i] = Ops.ToString(items[i]);
        }
        string line = string.Join(" ", parts);
        Action<string>? sink = TraceSink;
        if (sink != null)
        {
            sink(line);
        }
        else
        {
            Console.WriteLine(line);
        }
    }

    public static double ParseInt(object? text, double radix = 0)
    {
        string s = Ops.ToString(text).TrimStart();
        bool negative = false;
        int at = 0;
        if (at < s.Length && (s[at] == '+' || s[at] == '-'))
        {
            negative = s[at] == '-';
            at++;
        }
        int r = double.IsNaN(radix) ? 0 : (int)radix;
        if ((r == 0 || r == 16) && at + 1 < s.Length && s[at] == '0' && (s[at + 1] == 'x' || s[at + 1] == 'X'))
        {
            at += 2;
            r = 16;
        }
        if (r == 0)
        {
            r = 10;
        }
        if (r < 2 || r > 36)
        {
            return double.NaN;
        }
        double value = 0;
        int digits = 0;
        for (; at < s.Length; at++)
        {
            int digit = DigitValue(s[at]);
            if (digit < 0 || digit >= r)
            {
                break;
            }
            value = value * r + digit;
            digits++;
        }
        if (digits == 0)
        {
            return double.NaN;
        }
        return negative ? -value : value;
    }

    private static int DigitValue(char c)
    {
        if (c >= '0' && c <= '9')
        {
            return c - '0';
        }
        if (c >= 'a' && c <= 'z')
        {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'Z')
        {
            return c - 'A' + 10;
        }
        return -1;
    }

    public static double ParseFloat(object? text)
    {
        Match match = FloatPrefix.Match(Ops.ToString(text).TrimStart());
        if (!match.Success)
        {
            return double.NaN;
        }
        bool negative = match.Groups[1].Value == "-";
        if (match.Groups[2].Value == "Infinity")
        {
            return negative ? double.NegativeInfinity : double.PositiveInfinity;
        }
        double value = double.Parse(match.Groups[2].Value, NumberStyles.Float, CultureInfo.InvariantCulture);
        return negative ? -value : value;
    }
}
