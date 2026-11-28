using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;
using Flash;
using Xunit;

namespace Radicle.Interop.Tests;

[Collection("FlashStatics")]
public class FlashCoreTests
{
    private static AsObject Row(int n) => new AsObject { ["n"] = n };

    [Fact]
    public void ToBoolFollowsActionScriptTruthiness()
    {
        Assert.False(Ops.ToBool(null));
        Assert.False(Ops.ToBool(Undefined.Value));
        Assert.False(Ops.ToBool(0));
        Assert.False(Ops.ToBool(double.NaN));
        Assert.False(Ops.ToBool(string.Empty));
        Assert.False(Ops.ToBool(false));
        Assert.True(Ops.ToBool("0"));
        Assert.True(Ops.ToBool("false"));
        Assert.True(Ops.ToBool(-1.5));
        Assert.True(Ops.ToBool(double.PositiveInfinity));
        Assert.True(Ops.ToBool(new AsObject()));
    }

    [Fact]
    public void ToNumberConvertsStringsAndPrimitives()
    {
        Assert.Equal(0.0, Ops.ToNumber(string.Empty));
        Assert.Equal(42.0, Ops.ToNumber("  42  "));
        Assert.Equal(31.0, Ops.ToNumber("0x1F"));
        Assert.Equal(1000.0, Ops.ToNumber("1e3"));
        Assert.Equal(0.5, Ops.ToNumber(".5"));
        Assert.Equal(5.0, Ops.ToNumber("5."));
        Assert.Equal(3.0, Ops.ToNumber("+3"));
        Assert.Equal(double.NegativeInfinity, Ops.ToNumber("-Infinity"));
        Assert.Equal(0.0, Ops.ToNumber(null));
        Assert.Equal(1.0, Ops.ToNumber(true));
        Assert.True(double.IsNaN(Ops.ToNumber("12px")));
        Assert.True(double.IsNaN(Ops.ToNumber("0x")));
        Assert.True(double.IsNaN(Ops.ToNumber("-0x10")));
        Assert.True(double.IsNaN(Ops.ToNumber("1 2")));
        Assert.True(double.IsNaN(Ops.ToNumber(Undefined.Value)));
        Assert.True(double.IsNaN(Ops.ToNumber(new AsObject())));
    }

    [Fact]
    public void ToIntAndToUIntWrapModuloTwoToThe32()
    {
        Assert.Equal(int.MinValue, Ops.ToInt(2147483648.0));
        Assert.Equal(0, Ops.ToInt(4294967296.0));
        Assert.Equal(-1, Ops.ToInt(-1.9));
        Assert.Equal(3, Ops.ToInt(3.9));
        Assert.Equal(0, Ops.ToInt(double.NaN));
        Assert.Equal(0, Ops.ToInt(double.PositiveInfinity));
        Assert.Equal(12, Ops.ToInt("12"));
        Assert.Equal(4294967295u, Ops.ToUInt(-1));
        Assert.Equal(1u, Ops.ToUInt(4294967297.0));
        Assert.Equal(2147483648u, Ops.ToUInt(2147483648.5));
        Assert.Equal(7u, Ops.ToUInt("7"));
    }

    [Theory]
    [InlineData(100.0, "100")]
    [InlineData(0.5, "0.5")]
    [InlineData(123.456, "123.456")]
    [InlineData(-1.5, "-1.5")]
    [InlineData(1e20, "100000000000000000000")]
    [InlineData(1e21, "1e+21")]
    [InlineData(123456789012345680000.0, "123456789012345680000")]
    [InlineData(0.000001, "0.000001")]
    [InlineData(1e-7, "1e-7")]
    [InlineData(1.5e-10, "1.5e-10")]
    [InlineData(0.30000000000000004, "0.30000000000000004")]
    [InlineData(4294967296.0, "4294967296")]
    [InlineData(-0.0, "0")]
    [InlineData(double.NaN, "NaN")]
    [InlineData(double.PositiveInfinity, "Infinity")]
    [InlineData(double.NegativeInfinity, "-Infinity")]
    public void NumberToStringFollowsTheEcmaFormatRules(double value, string expected)
    {
        Assert.Equal(expected, Ops.NumberToString(value));
    }

    [Fact]
    public void ToStringCoversNullUndefinedAndPrimitives()
    {
        Assert.Equal("null", Ops.ToString(null));
        Assert.Equal("undefined", Ops.ToString(Undefined.Value));
        Assert.Equal("true", Ops.ToString(true));
        Assert.Equal("5", Ops.ToString(5));
        Assert.Equal("2.5", Ops.ToString(2.5f));
        Assert.Equal("7", Ops.ToString(7u));
        Assert.Equal("x", Ops.ToString("x"));
        Assert.Equal("1,2", Ops.ToString(new AsArray(1, 2)));
        Assert.Equal(",,", Ops.ToString(new AsArray(3)));
        Assert.Equal("[object Object]", Ops.ToString(new AsObject()));
    }

    [Fact]
    public void StrictEqualsComparesNumbersAcrossTypesAndNeverCoerces()
    {
        Assert.True(Ops.StrictEquals(1, 1.0));
        Assert.True(Ops.StrictEquals("a", "a"));
        Assert.True(Ops.StrictEquals(null, null));
        Assert.False(Ops.StrictEquals(1, "1"));
        Assert.False(Ops.StrictEquals(true, 1));
        Assert.False(Ops.StrictEquals(null, Undefined.Value));
        Assert.False(Ops.StrictEquals(double.NaN, double.NaN));
    }

    [Fact]
    public void InvokeAdaptsArityAndUnwrapsExceptions()
    {
        Func<int, int> increment = x => x + 1;
        Func<int, int, int> add = (a, b) => a + b;
        Action thrower = () => throw new InvalidOperationException("boom");
        Assert.Equal(6, Ops.Invoke(increment, 5, "ignored"));
        Assert.Equal(5, Ops.Invoke(add, 5));
        Assert.Throws<InvalidOperationException>(() => Ops.Invoke(thrower));
    }

    [Fact]
    public void MathMatchesActionScript()
    {
        Assert.Equal(1.0, AsMath.Round(0.5));
        Assert.Equal(3.0, AsMath.Round(2.5));
        Assert.Equal(-2.0, AsMath.Round(-2.5));
        Assert.Equal(double.NegativeInfinity, AsMath.Max());
        Assert.Equal(double.PositiveInfinity, AsMath.Min());
        Assert.Equal(7.0, AsMath.Max(2, 7, 5));
        Assert.Equal(2.0, AsMath.Min(2, 7, 5));
        Assert.True(double.IsNaN(AsMath.Max(1, double.NaN, 3)));
        Assert.Equal(1024.0, AsMath.Pow(2, 10));
        Assert.Equal(Math.PI / 4, AsMath.Atan2(1, 1), 12);
        for (int i = 0; i < 100; i++)
        {
            Assert.InRange(AsMath.Random(), 0.0, 1.0);
        }
    }

    [Fact]
    public void ParseIntAndParseFloatFollowActionScript()
    {
        Assert.Equal(42.0, Globals.ParseInt("42px"));
        Assert.Equal(31.0, Globals.ParseInt("0x1F"));
        Assert.Equal(-17.0, Globals.ParseInt("  -17"));
        Assert.Equal(5.0, Globals.ParseInt("101", 2));
        Assert.Equal(35.0, Globals.ParseInt("z", 36));
        Assert.Equal(8.0, Globals.ParseInt("08"));
        Assert.Equal(1.0, Globals.ParseInt("1e3"));
        Assert.True(double.IsNaN(Globals.ParseInt("abc")));
        Assert.True(double.IsNaN(Globals.ParseInt("0x", 16)));
        Assert.True(double.IsNaN(Globals.ParseInt("12", 1)));
        Assert.True(double.IsNaN(Globals.ParseInt(null)));
        Assert.Equal(3.14, Globals.ParseFloat("3.14abc"));
        Assert.Equal(0.5, Globals.ParseFloat(".5"));
        Assert.Equal(1000.0, Globals.ParseFloat("1e3"));
        Assert.Equal(1.0, Globals.ParseFloat("1e"));
        Assert.Equal(7.0, Globals.ParseFloat("  7 "));
        Assert.Equal(double.NegativeInfinity, Globals.ParseFloat("-Infinityx"));
        Assert.True(double.IsNaN(Globals.ParseFloat("abc")));
    }

    [Fact]
    public void TraceJoinsArgumentsWithSpacesAndWritesToTheSink()
    {
        List<string> lines = new List<string>();
        Globals.TraceSink = lines.Add;
        try
        {
            Globals.Trace("a", 1, null, true);
            Globals.Trace();
            Globals.Trace(null);
        }
        finally
        {
            Globals.TraceSink = null;
        }
        Assert.Equal(new[] { "a 1 null true", string.Empty, "null" }, lines);
    }

    [Fact]
    public void StringFunctionsUseActionScriptIndexRules()
    {
        Assert.Equal("el", StringExtensions.Substring("hello", 3, 1));
        Assert.Equal("hello", StringExtensions.Substring("hello", -2, 99));
        Assert.Equal("ll", StringExtensions.Substr("hello", -3, 2));
        Assert.Equal("ello", StringExtensions.Substr("hello", 1));
        Assert.Equal("llo", StringExtensions.Slice("hello", -3));
        Assert.Equal("ell", StringExtensions.Slice("hello", 1, -1));
        Assert.Equal(string.Empty, StringExtensions.Slice("hello", 3, 1));
        Assert.Equal(string.Empty, StringExtensions.CharAt("abc", 5));
        Assert.Equal(65.0, StringExtensions.CharCodeAt("A"));
        Assert.True(double.IsNaN(StringExtensions.CharCodeAt("abc", 9)));
        Assert.Equal(3, StringExtensions.IndexOf("hello", "l", 3));
        Assert.Equal(3, StringExtensions.LastIndexOf("hello", "l"));
        Assert.Equal(2, StringExtensions.LastIndexOf("hello", "l", 2));
        Assert.Equal(-1, StringExtensions.LastIndexOf("hello", "z"));
        Assert.Equal("a1true", StringExtensions.Concat("a", 1, true));
    }

    [Fact]
    public void SplitReplaceMatchAndSearchBehaveLikeActionScript()
    {
        Assert.Equal("a|b|c", StringExtensions.Split("a,b,c", ",").Join("|"));
        Assert.Equal("a|b|c", StringExtensions.Split("abc", string.Empty).Join("|"));
        Assert.Equal(2, StringExtensions.Split("a,b,c", ",", 2).Length);
        Assert.Equal("x", StringExtensions.Split("x").Join());
        Assert.Equal("a-bX", StringExtensions.Replace("aXbX", "X", "-"));
        Assert.Equal("a-b-", StringExtensions.Replace("aXbX", new Regex("X"), "-"));
        Assert.Equal("$1", StringExtensions.Replace("a", new Regex("a"), "$1"));
        AsArray? found = StringExtensions.Match("abc123", new Regex("([a-z]+)(\\d+)"));
        Assert.NotNull(found);
        Assert.Equal("abc123|abc|123", found!.Join("|"));
        Assert.Null(StringExtensions.Match("abc", new Regex("\\d")));
        Assert.Equal(2, StringExtensions.Search("abc", "c"));
        Assert.Equal(-1, StringExtensions.Search("abc", "z"));
    }

    [Fact]
    public void FunctionCallAndApplyForwardArguments()
    {
        Func<int, int, int> add = (a, b) => a + b;
        Assert.Equal(7, FunctionExtensions.Call(add, null, 3, 4));
        Assert.Equal(9, add.Apply(null, new AsArray(4, 5)));
    }

    [Fact]
    public void AsObjectSupportsTypedAndDynamicAccess()
    {
        AsObject typed = new AsObject { ["a"] = 1, ["b"] = "two" };
        Assert.Equal(2, typed.Count);
        Assert.Equal(new[] { "a", "b" }, typed.Keys.ToArray());
        Assert.True(typed.HasOwnProperty("a"));
        Assert.Null(typed["missing"]);
        Assert.True(typed.Remove("a"));
        Assert.False(typed.HasOwnProperty("a"));
        Assert.Equal("[object Object]", typed.ToString());

        dynamic bag = new AsObject();
        bag.name = "hero";
        bag.hp = 3;
        bag.twice = (Func<int, int>)(x => x * 2);
        Assert.Equal("hero", (string)bag.name);
        Assert.Equal(3, (int)bag.hp);
        Assert.Equal(8, (int)bag.twice(4));
        Assert.True((bool)bag.HasOwnProperty("name"));
        Assert.Null((object?)bag.missing);
    }

    [Fact]
    public void AsArrayConstructorLengthIndexAndNamedKeys()
    {
        Assert.Equal(3, new AsArray(3).Length);
        Assert.Equal("x", new AsArray("x").Join());
        Assert.Throws<ArgumentOutOfRangeException>(() => new AsArray(1.5));
        Assert.Throws<ArgumentOutOfRangeException>(() => new AsArray(-1));
        AsArray sparse = new AsArray { "a" };
        sparse[3] = "d";
        Assert.Equal(4, sparse.Length);
        Assert.Null(sparse[2]);
        Assert.Equal("d", sparse["3"]);
        sparse["foo"] = 7;
        Assert.Equal(7, sparse["foo"]);
        Assert.Equal(4, sparse.Length);
        sparse.Length = 2;
        Assert.Equal("a,", sparse.Join());
    }

    [Fact]
    public void AsArrayStackAndQueueOperations()
    {
        AsArray a = new AsArray { 1, 2 };
        Assert.Equal(3, a.Push(3));
        Assert.Equal(5, a.Unshift(-1, 0));
        Assert.Equal(-1, a.Shift());
        Assert.Equal(3, a.Pop());
        Assert.Equal("0,1,2", a.Join());
        Assert.Null(new AsArray().Pop());
        Assert.Equal(4, a.Push(null));
        Assert.Null(a.Pop());
    }

    [Fact]
    public void AsArraySpliceSliceAndConcatUseNegativeAndDefaultArguments()
    {
        AsArray a = new AsArray { 1, 2, 3, 4, 5 };
        Assert.Equal("2,3", a.Splice(1, 2).Join());
        Assert.Equal("1,4,5", a.Join());
        a.Splice(1, 0, "x", "y");
        Assert.Equal("1,x,y,4,5", a.Join());
        AsArray tail = new AsArray { 1, 2, 3 };
        Assert.Equal("3", tail.Splice(-1, 1).Join());
        Assert.Equal("1,2", tail.Join());
        Assert.Equal("2", tail.Splice(1).Join());
        AsArray four = new AsArray { 1, 2, 3, 4 };
        Assert.Equal("2,3", four.Slice(1, 3).Join());
        Assert.Equal("3,4", four.Slice(-2).Join());
        Assert.Equal("1,2,3,4", four.Slice().Join());
        Assert.Empty(four.Slice(2, 1));
        Assert.Equal("1,2,3,4", new AsArray { 1 }.Concat(new AsArray { 2, 3 }, 4).Join());
        Assert.Equal("1,,3", new AsArray { 1, null, 3 }.Join());
        Assert.Equal("1-2", new AsArray { 1, 2 }.Join("-"));
    }

    [Fact]
    public void AsArraySortIsStableAndHonoursFlags()
    {
        AsArray numbers = new AsArray(10, 9, 1);
        numbers.Sort();
        Assert.Equal("1,10,9", numbers.Join());
        numbers.Sort(AsArray.NUMERIC);
        Assert.Equal("1,9,10", numbers.Join());
        numbers.Sort(AsArray.NUMERIC | AsArray.DESCENDING);
        Assert.Equal("10,9,1", numbers.Join());

        AsArray words = new AsArray("b", "B", "a");
        words.Sort(AsArray.CASEINSENSITIVE);
        Assert.Equal("a,b,B", words.Join());
        words = new AsArray("b", "B", "a");
        words.Sort();
        Assert.Equal("B,a,b", words.Join());

        Func<object?, object?, int> byLength = (a, b) => ((string)a!).Length - ((string)b!).Length;
        AsArray items = new AsArray("bb", "a", "cc", "d");
        items.Sort(byLength);
        Assert.Equal("a,d,bb,cc", items.Join());

        Assert.Throws<NotSupportedException>(() => new AsArray { 1, 2 }.Sort(AsArray.UNIQUESORT));
    }

    [Fact]
    public void AsArraySortOnOrdersObjectsByField()
    {
        AsArray rows = new AsArray { Row(3), Row(1), Row(2) };
        rows.SortOn("n", AsArray.NUMERIC);
        Assert.Equal(new object?[] { 1, 2, 3 }, rows.Select(r => ((AsObject)r!)["n"]).ToArray());
    }

    [Fact]
    public void AsArrayIndexOfUsesStrictEquality()
    {
        AsArray a = new AsArray { 1, "1", 1 };
        Assert.Equal(1, a.IndexOf("1"));
        Assert.Equal(2, a.IndexOf(1, 1));
        Assert.Equal(2, a.LastIndexOf(1));
        Assert.Equal(-1, a.IndexOf(5));
        Assert.Equal(-1, new AsArray().LastIndexOf(1));
    }

    [Fact]
    public void AsArrayCallbacksReceiveItemIndexAndArrayAndDropExtras()
    {
        AsArray a = new AsArray { 1, 2, 3, 4 };
        Func<object?, object?> doubled = x => Ops.ToNumber(x) * 2;
        Func<object?, int, object?> indexOnly = (x, i) => i;
        Func<object?, bool> even = x => Ops.ToNumber(x) % 2 == 0;
        Assert.Equal("2,4,6,8", a.Map(doubled).Join());
        Assert.Equal("0,1,2,3", a.Map(indexOnly).Join());
        Assert.Equal("2,4", a.Filter(even).Join());
        Assert.True(a.Some(even));
        Assert.False(a.Every(even));
        List<string> seen = new List<string>();
        Action<object?, int, AsArray> visit = (x, i, array) => seen.Add(i + ":" + x + ":" + array.Length);
        new AsArray { "a", "b" }.ForEach(visit);
        Assert.Equal(new[] { "0:a:2", "1:b:2" }, seen);
    }

    [Fact]
    public void AsVectorEnforcesBoundsAndFixedLength()
    {
        AsVector<int> v = new AsVector<int>(3);
        Assert.Equal(3, v.Length);
        Assert.Equal(0, v[1]);
        v[3] = 9;
        Assert.Equal(4, v.Length);
        Assert.Throws<ArgumentOutOfRangeException>(() => { _ = v[10]; });

        AsVector<string> fixedVector = new AsVector<string>(2, true);
        Assert.Throws<InvalidOperationException>(() => { fixedVector.Push("x"); });
        Assert.Throws<InvalidOperationException>(() => { fixedVector.Length = 5; });
        fixedVector[0] = "a";
        Assert.Equal("a", fixedVector[0]);
        fixedVector.Fixed = false;
        fixedVector.Push("z");
        Assert.Equal(3, fixedVector.Length);
    }

    [Fact]
    public void AsVectorSortSpliceSliceAndConcat()
    {
        AsVector<int> n = new AsVector<int>();
        n.Push(3, 1, 2);
        Func<int, int, int> ascending = (a, b) => a - b;
        n.Sort(ascending);
        Assert.Equal("1,2,3", n.Join());
        Assert.Equal("2", n.Splice(1, 1).Join());
        Assert.Equal("1,3", n.Join());
        AsVector<int> m = new AsVector<int>();
        m.Push(4);
        Assert.Equal(new[] { 1, 3, 4 }, n.Concat(m).ToList());
        Assert.Equal("3,4", n.Concat(m).Slice(1).Join());
        Func<int, bool> big = x => x > 2;
        Assert.Equal(new[] { 3 }, n.Filter(big).ToList());
        Assert.True(n.Some(big));
        Assert.False(n.Every(big));
        Assert.Equal(1, n.IndexOf(3));
    }
}
