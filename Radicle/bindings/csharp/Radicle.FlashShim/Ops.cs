using System;
using System.Globalization;
using System.Reflection;
using System.Runtime.ExceptionServices;
using System.Text.RegularExpressions;

namespace Flash;

public sealed class Undefined
{
    public static readonly Undefined Value = new Undefined();

    private Undefined()
    {
    }

    public override string ToString() => "undefined";
}

public static class Ops
{
    private static readonly Regex DecimalNumber =
        new Regex(@"^[+-]?([0-9]+\.?[0-9]*|\.[0-9]+)([eE][+-]?[0-9]+)?\z", RegexOptions.CultureInvariant);

    public static bool IsNumeric(object? value)
    {
        return value is double || value is float || value is int || value is uint || value is long ||
               value is ulong || value is short || value is ushort || value is byte || value is sbyte ||
               value is decimal;
    }

    internal static object?[] Rest(object?[]? args) => args ?? new object?[] { null };

    public static bool ToBool(object? value)
    {
        switch (value)
        {
            case null: return false;
            case Undefined: return false;
            case bool b: return b;
            case string s: return s.Length != 0;
            case double d: return !(double.IsNaN(d) || d == 0);
            case float f: return !(float.IsNaN(f) || f == 0);
            case int i: return i != 0;
            case uint u: return u != 0;
            case long l: return l != 0;
            case ulong ul: return ul != 0;
            case short sh: return sh != 0;
            case ushort us: return us != 0;
            case byte by: return by != 0;
            case sbyte sb: return sb != 0;
            case decimal m: return m != 0;
            default: return true;
        }
    }

    public static double ToNumber(object? value)
    {
        switch (value)
        {
            case null: return 0;
            case Undefined: return double.NaN;
            case bool b: return b ? 1 : 0;
            case double d: return d;
            case float f: return f;
            case int i: return i;
            case uint u: return u;
            case long l: return l;
            case ulong ul: return ul;
            case short sh: return sh;
            case ushort us: return us;
            case byte by: return by;
            case sbyte sb: return sb;
            case decimal m: return (double)m;
            case string s: return StringToNumber(s);
            default: return double.NaN;
        }
    }

    public static double StringToNumber(string text)
    {
        string t = text.Trim();
        if (t.Length == 0)
        {
            return 0;
        }
        if (t == "Infinity" || t == "+Infinity")
        {
            return double.PositiveInfinity;
        }
        if (t == "-Infinity")
        {
            return double.NegativeInfinity;
        }
        if (t.Length > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X'))
        {
            double accumulated = 0;
            for (int i = 2; i < t.Length; i++)
            {
                int digit = HexDigit(t[i]);
                if (digit < 0)
                {
                    return double.NaN;
                }
                accumulated = accumulated * 16 + digit;
            }
            return accumulated;
        }
        if (!DecimalNumber.IsMatch(t))
        {
            return double.NaN;
        }
        return double.Parse(t, NumberStyles.Float, CultureInfo.InvariantCulture);
    }

    private static int HexDigit(char c)
    {
        if (c >= '0' && c <= '9')
        {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f')
        {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F')
        {
            return c - 'A' + 10;
        }
        return -1;
    }

    private static double WrapToUInt32(double d)
    {
        if (double.IsNaN(d) || double.IsInfinity(d))
        {
            return 0;
        }
        double remainder = Math.Truncate(d) % 4294967296.0;
        return remainder < 0 ? remainder + 4294967296.0 : remainder;
    }

    public static int ToInt(object? value)
    {
        if (value is int exact)
        {
            return exact;
        }
        return unchecked((int)(uint)WrapToUInt32(ToNumber(value)));
    }

    public static uint ToUInt(object? value)
    {
        if (value is uint exact)
        {
            return exact;
        }
        return unchecked((uint)WrapToUInt32(ToNumber(value)));
    }

    public static string ToString(object? value)
    {
        switch (value)
        {
            case null: return "null";
            case Undefined: return "undefined";
            case string s: return s;
            case bool b: return b ? "true" : "false";
            case double d: return NumberToString(d);
            case float f:
                return NumberToString(double.Parse(f.ToString("R", CultureInfo.InvariantCulture), CultureInfo.InvariantCulture));
            case IFormattable formattable: return formattable.ToString(null, CultureInfo.InvariantCulture);
            default: return value.ToString() ?? string.Empty;
        }
    }

    public static string NumberToString(double d)
    {
        if (double.IsNaN(d))
        {
            return "NaN";
        }
        if (double.IsPositiveInfinity(d))
        {
            return "Infinity";
        }
        if (double.IsNegativeInfinity(d))
        {
            return "-Infinity";
        }
        if (d == 0)
        {
            return "0";
        }
        string sign = d < 0 ? "-" : string.Empty;
        string text = Math.Abs(d).ToString("R", CultureInfo.InvariantCulture);
        int exponent = 0;
        int e = text.IndexOf('E');
        if (e >= 0)
        {
            exponent = int.Parse(text.Substring(e + 1), CultureInfo.InvariantCulture);
            text = text.Substring(0, e);
        }
        int dot = text.IndexOf('.');
        int point = (dot < 0 ? text.Length : dot) + exponent;
        string digits = dot < 0 ? text : text.Remove(dot, 1);
        int lead = 0;
        while (lead < digits.Length - 1 && digits[lead] == '0')
        {
            lead++;
        }
        digits = digits.Substring(lead).TrimEnd('0');
        point -= lead;
        int k = digits.Length;
        if (k <= point && point <= 21)
        {
            return sign + digits + new string('0', point - k);
        }
        if (0 < point && point <= 21)
        {
            return sign + digits.Substring(0, point) + "." + digits.Substring(point);
        }
        if (-6 < point && point <= 0)
        {
            return sign + "0." + new string('0', -point) + digits;
        }
        int power = point - 1;
        string powerText = (power < 0 ? "-" : "+") + Math.Abs(power).ToString(CultureInfo.InvariantCulture);
        string mantissa = k == 1 ? digits : digits.Substring(0, 1) + "." + digits.Substring(1);
        return sign + mantissa + "e" + powerText;
    }

    public static bool StrictEquals(object? a, object? b)
    {
        if (IsNumeric(a) && IsNumeric(b))
        {
            return ToNumber(a) == ToNumber(b);
        }
        if (a == null || b == null)
        {
            return a == null && b == null;
        }
        if (a is string sa)
        {
            return b is string sb && sa == sb;
        }
        if (a is bool ba)
        {
            return b is bool bb && ba == bb;
        }
        return a.Equals(b);
    }

    public static object? Invoke(Delegate callback, params object?[] args)
    {
        MethodInfo? invoke = callback.GetType().GetMethod("Invoke");
        ParameterInfo[] parameters = invoke == null ? Array.Empty<ParameterInfo>() : invoke.GetParameters();
        object?[] call = new object?[parameters.Length];
        for (int i = 0; i < call.Length; i++)
        {
            if (i < args.Length)
            {
                call[i] = args[i];
            }
            else if (parameters[i].ParameterType.IsValueType)
            {
                call[i] = Activator.CreateInstance(parameters[i].ParameterType);
            }
        }
        try
        {
            return callback.DynamicInvoke(call);
        }
        catch (TargetInvocationException error) when (error.InnerException != null)
        {
            ExceptionDispatchInfo.Capture(error.InnerException).Throw();
            throw;
        }
    }
}
