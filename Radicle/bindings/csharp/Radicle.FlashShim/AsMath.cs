using System;

namespace Flash;

public static class AsMath
{
    public const double E = Math.E;
    public const double LN10 = 2.302585092994046;
    public const double LN2 = 0.6931471805599453;
    public const double LOG10E = 0.4342944819032518;
    public const double LOG2E = 1.4426950408889634;
    public const double PI = Math.PI;
    public const double SQRT1_2 = 0.7071067811865476;
    public const double SQRT2 = 1.4142135623730951;

    public static double Abs(double value) => Math.Abs(value);

    public static double Ceil(double value) => Math.Ceiling(value);

    public static double Floor(double value) => Math.Floor(value);

    public static double Round(double value) => Math.Floor(value + 0.5);

    public static double Pow(double x, double y) => Math.Pow(x, y);

    public static double Sqrt(double value) => Math.Sqrt(value);

    public static double Sin(double value) => Math.Sin(value);

    public static double Cos(double value) => Math.Cos(value);

    public static double Tan(double value) => Math.Tan(value);

    public static double Asin(double value) => Math.Asin(value);

    public static double Acos(double value) => Math.Acos(value);

    public static double Atan(double value) => Math.Atan(value);

    public static double Atan2(double y, double x) => Math.Atan2(y, x);

    public static double Exp(double value) => Math.Exp(value);

    public static double Log(double value) => Math.Log(value);

    public static double Max(params double[] values)
    {
        double result = double.NegativeInfinity;
        foreach (double value in values)
        {
            result = double.IsNaN(value) || double.IsNaN(result) ? double.NaN : Math.Max(result, value);
        }
        return result;
    }

    public static double Min(params double[] values)
    {
        double result = double.PositiveInfinity;
        foreach (double value in values)
        {
            result = double.IsNaN(value) || double.IsNaN(result) ? double.NaN : Math.Min(result, value);
        }
        return result;
    }

    public static double Random() => global::System.Random.Shared.NextDouble();
}
