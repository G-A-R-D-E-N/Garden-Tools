using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace Flash;

public class AsArray : IEnumerable<object?>
{
    public const int CASEINSENSITIVE = 1;
    public const int DESCENDING = 2;
    public const int UNIQUESORT = 4;
    public const int RETURNINDEXEDARRAY = 8;
    public const int NUMERIC = 16;

    private const int MaxDenseIndex = 16777215;

    private readonly List<object?> _items = new List<object?>();
    private readonly Dictionary<string, object?> _named = new Dictionary<string, object?>();

    public AsArray()
    {
    }

    public AsArray(params object?[] items)
    {
        if (items.Length == 1 && Ops.IsNumeric(items[0]))
        {
            double length = Ops.ToNumber(items[0]);
            if (length < 0 || length != Math.Floor(length) || length > MaxDenseIndex)
            {
                throw new ArgumentOutOfRangeException(nameof(items), "RangeError: Error #1005: Array index is not a positive integer.");
            }
            Length = (int)length;
        }
        else
        {
            _items.AddRange(items);
        }
    }

    internal static AsArray Of(IEnumerable<object?> items)
    {
        AsArray result = new AsArray();
        result._items.AddRange(items);
        return result;
    }

    public int Length
    {
        get => _items.Count;
        set
        {
            if (value < 0 || value > MaxDenseIndex + 1)
            {
                throw new ArgumentOutOfRangeException(nameof(value));
            }
            if (value < _items.Count)
            {
                _items.RemoveRange(value, _items.Count - value);
            }
            while (_items.Count < value)
            {
                _items.Add(null);
            }
        }
    }

    public object? this[object? index]
    {
        get
        {
            if (TryIndex(index, out int position))
            {
                return position < _items.Count ? _items[position] : null;
            }
            return _named.TryGetValue(Ops.ToString(index), out object? value) ? value : null;
        }
        set
        {
            if (TryIndex(index, out int position))
            {
                if (position >= _items.Count)
                {
                    Length = position + 1;
                }
                _items[position] = value;
            }
            else
            {
                _named[Ops.ToString(index)] = value;
            }
        }
    }

    private static bool TryIndex(object? key, out int position)
    {
        position = 0;
        if (Ops.IsNumeric(key))
        {
            double d = Ops.ToNumber(key);
            if (d >= 0 && d == Math.Floor(d) && d <= MaxDenseIndex)
            {
                position = (int)d;
                return true;
            }
            return false;
        }
        return key is string text && int.TryParse(text, NumberStyles.None, CultureInfo.InvariantCulture, out position) &&
               position <= MaxDenseIndex;
    }

    private static int Resolve(double index, int count)
    {
        if (double.IsNaN(index))
        {
            return 0;
        }
        double t = Math.Truncate(index);
        if (t < 0)
        {
            t += count;
        }
        if (t < 0)
        {
            return 0;
        }
        return t > count ? count : (int)t;
    }

    public void Add(object? item) => _items.Add(item);

    public int Push(params object?[]? items)
    {
        _items.AddRange(Ops.Rest(items));
        return _items.Count;
    }

    public object? Pop()
    {
        if (_items.Count == 0)
        {
            return null;
        }
        object? last = _items[_items.Count - 1];
        _items.RemoveAt(_items.Count - 1);
        return last;
    }

    public object? Shift()
    {
        if (_items.Count == 0)
        {
            return null;
        }
        object? first = _items[0];
        _items.RemoveAt(0);
        return first;
    }

    public int Unshift(params object?[]? items)
    {
        _items.InsertRange(0, Ops.Rest(items));
        return _items.Count;
    }

    public AsArray Splice(double startIndex, double deleteCount = double.PositiveInfinity, params object?[]? values)
    {
        int start = Resolve(startIndex, _items.Count);
        double wanted = double.IsNaN(deleteCount) ? 0 : Math.Truncate(deleteCount);
        int remove = wanted <= 0 ? 0 : (int)Math.Min(wanted, _items.Count - start);
        AsArray removed = Of(_items.GetRange(start, remove));
        _items.RemoveRange(start, remove);
        if (values == null || values.Length > 0)
        {
            _items.InsertRange(start, Ops.Rest(values));
        }
        return removed;
    }

    public AsArray Slice(double startIndex = 0, double endIndex = 16777215)
    {
        int start = Resolve(startIndex, _items.Count);
        int end = Resolve(endIndex, _items.Count);
        return end <= start ? new AsArray() : Of(_items.GetRange(start, end - start));
    }

    public AsArray Concat(params object?[]? values)
    {
        AsArray result = Of(_items);
        foreach (object? value in Ops.Rest(values))
        {
            if (value is AsArray other)
            {
                result._items.AddRange(other._items);
            }
            else
            {
                result._items.Add(value);
            }
        }
        return result;
    }

    internal static string JoinText(object? value) => (value == null || (value is Undefined)) ? string.Empty : Ops.ToString(value);

    public string Join(object? separator = null)
    {
        string glue = separator == null ? "," : Ops.ToString(separator);
        StringBuilder builder = new StringBuilder();
        for (int i = 0; i < _items.Count; i++)
        {
            if (i > 0)
            {
                builder.Append(glue);
            }
            builder.Append(JoinText(_items[i]));
        }
        return builder.ToString();
    }

    public AsArray Reverse()
    {
        _items.Reverse();
        return this;
    }

    private static void StableSort(List<object?> items, Comparison<object?> compare)
    {
        int[] order = new int[items.Count];
        for (int i = 0; i < order.Length; i++)
        {
            order[i] = i;
        }
        List<object?> snapshot = new List<object?>(items);
        Array.Sort(order, (x, y) =>
        {
            int result = compare(snapshot[x], snapshot[y]);
            return result != 0 ? result : x.CompareTo(y);
        });
        for (int i = 0; i < order.Length; i++)
        {
            items[i] = snapshot[order[i]];
        }
    }

    private static int CompareValues(object? a, object? b, int options)
    {
        int result;
        if ((options & NUMERIC) != 0)
        {
            result = Ops.ToNumber(a).CompareTo(Ops.ToNumber(b));
        }
        else if ((options & CASEINSENSITIVE) != 0)
        {
            result = string.Compare(Ops.ToString(a), Ops.ToString(b), StringComparison.OrdinalIgnoreCase);
        }
        else
        {
            result = string.CompareOrdinal(Ops.ToString(a), Ops.ToString(b));
        }
        return (options & DESCENDING) != 0 ? -result : result;
    }

    public AsArray Sort(params object?[]? arguments)
    {
        Delegate? compare = null;
        int options = 0;
        foreach (object? argument in arguments ?? Array.Empty<object?>())
        {
            if (argument is Delegate callback)
            {
                compare = callback;
            }
            else if (argument != null)
            {
                options = Ops.ToInt(argument);
            }
        }
        if ((options & (UNIQUESORT | RETURNINDEXEDARRAY)) != 0)
        {
            throw new NotSupportedException("Array.UNIQUESORT and Array.RETURNINDEXEDARRAY are not implemented.");
        }
        if (compare != null)
        {
            Delegate function = compare;
            StableSort(_items, (a, b) =>
            {
                double outcome = Ops.ToNumber(Ops.Invoke(function, a, b));
                return outcome < 0 ? -1 : outcome > 0 ? 1 : 0;
            });
        }
        else
        {
            int flags = options;
            StableSort(_items, (a, b) => CompareValues(a, b, flags));
        }
        return this;
    }

    private static object? Field(object? item, string name) => item is AsObject bag ? bag[name] : null;

    public AsArray SortOn(object? fieldName, object? options = null)
    {
        List<string> names = new List<string>();
        if (fieldName is AsArray list)
        {
            foreach (object? entry in list._items)
            {
                names.Add(Ops.ToString(entry));
            }
        }
        else
        {
            names.Add(Ops.ToString(fieldName));
        }
        StableSort(_items, (a, b) =>
        {
            for (int i = 0; i < names.Count; i++)
            {
                object? flag = options is AsArray flags ? flags[i] : options;
                int result = CompareValues(Field(a, names[i]), Field(b, names[i]), Ops.ToInt(flag));
                if (result != 0)
                {
                    return result;
                }
            }
            return 0;
        });
        return this;
    }

    public int IndexOf(object? item, double fromIndex = 0)
    {
        for (int i = Resolve(fromIndex, _items.Count); i < _items.Count; i++)
        {
            if (Ops.StrictEquals(_items[i], item))
            {
                return i;
            }
        }
        return -1;
    }

    public int LastIndexOf(object? item, double fromIndex = 2147483647)
    {
        double start = fromIndex < 0 ? fromIndex + _items.Count : Math.Min(fromIndex, _items.Count - 1);
        for (int i = (int)Math.Min(start, _items.Count - 1); i >= 0; i--)
        {
            if (Ops.StrictEquals(_items[i], item))
            {
                return i;
            }
        }
        return -1;
    }

    public void ForEach(Delegate callback, object? thisObject = null)
    {
        for (int i = 0; i < _items.Count; i++)
        {
            Ops.Invoke(callback, _items[i], i, this);
        }
    }

    public AsArray Map(Delegate callback, object? thisObject = null)
    {
        AsArray result = new AsArray();
        for (int i = 0; i < _items.Count; i++)
        {
            result._items.Add(Ops.Invoke(callback, _items[i], i, this));
        }
        return result;
    }

    public AsArray Filter(Delegate callback, object? thisObject = null)
    {
        AsArray result = new AsArray();
        for (int i = 0; i < _items.Count; i++)
        {
            if (Ops.ToBool(Ops.Invoke(callback, _items[i], i, this)))
            {
                result._items.Add(_items[i]);
            }
        }
        return result;
    }

    public bool Some(Delegate callback, object? thisObject = null)
    {
        for (int i = 0; i < _items.Count; i++)
        {
            if (Ops.ToBool(Ops.Invoke(callback, _items[i], i, this)))
            {
                return true;
            }
        }
        return false;
    }

    public bool Every(Delegate callback, object? thisObject = null)
    {
        for (int i = 0; i < _items.Count; i++)
        {
            if (!Ops.ToBool(Ops.Invoke(callback, _items[i], i, this)))
            {
                return false;
            }
        }
        return true;
    }

    public object?[] ToArray() => _items.ToArray();

    public override string ToString() => Join();

    public IEnumerator<object?> GetEnumerator() => ((IEnumerable<object?>)_items).GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}
