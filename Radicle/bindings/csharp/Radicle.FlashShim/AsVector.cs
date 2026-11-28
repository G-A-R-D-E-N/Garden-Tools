using System;
using System.Collections;
using System.Collections.Generic;
using System.Text;

namespace Flash;

public abstract class AsVector
{
    public abstract int Length { get; set; }

    public abstract bool Fixed { get; set; }
}

public class AsVector<T> : AsVector, IEnumerable<T>
{
    private readonly List<T> _items = new List<T>();
    private bool _fixed;

    public AsVector(double length = 0, bool fixedLength = false)
    {
        if (length < 0 || length != Math.Floor(length) || length > 16777215)
        {
            throw new ArgumentOutOfRangeException(nameof(length), "RangeError: Error #1005: Array index is not a positive integer.");
        }
        for (int i = 0; i < (int)length; i++)
        {
            _items.Add(default!);
        }
        _fixed = fixedLength;
    }

    public override bool Fixed
    {
        get => _fixed;
        set => _fixed = value;
    }

    public override int Length
    {
        get => _items.Count;
        set
        {
            if (_fixed || value < 0)
            {
                throw new InvalidOperationException("RangeError: Error #1126: Cannot change the length of a fixed Vector.");
            }
            if (value < _items.Count)
            {
                _items.RemoveRange(value, _items.Count - value);
            }
            while (_items.Count < value)
            {
                _items.Add(default!);
            }
        }
    }

    public T this[double index]
    {
        get => _items[Position(index)];
        set
        {
            if (index == _items.Count && index == Math.Floor(index))
            {
                CheckGrowable();
                _items.Add(value);
            }
            else
            {
                _items[Position(index)] = value;
            }
        }
    }

    private int Position(double index)
    {
        if (index != Math.Floor(index) || index < 0 || index >= _items.Count)
        {
            throw new ArgumentOutOfRangeException(nameof(index), "RangeError: Error #1125: The index is out of range.");
        }
        return (int)index;
    }

    private void CheckGrowable()
    {
        if (_fixed)
        {
            throw new InvalidOperationException("RangeError: Error #1126: Cannot change the length of a fixed Vector.");
        }
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

    public void Add(T item)
    {
        CheckGrowable();
        _items.Add(item);
    }

    public int Push(params T[] items)
    {
        CheckGrowable();
        _items.AddRange(items);
        return _items.Count;
    }

    public T Pop()
    {
        CheckGrowable();
        if (_items.Count == 0)
        {
            return default!;
        }
        T last = _items[_items.Count - 1];
        _items.RemoveAt(_items.Count - 1);
        return last;
    }

    public T Shift()
    {
        CheckGrowable();
        if (_items.Count == 0)
        {
            return default!;
        }
        T first = _items[0];
        _items.RemoveAt(0);
        return first;
    }

    public int Unshift(params T[] items)
    {
        CheckGrowable();
        _items.InsertRange(0, items);
        return _items.Count;
    }

    public AsVector<T> Splice(double startIndex, double deleteCount = double.PositiveInfinity, params T[] values)
    {
        CheckGrowable();
        int start = Resolve(startIndex, _items.Count);
        double wanted = double.IsNaN(deleteCount) ? 0 : Math.Truncate(deleteCount);
        int remove = wanted <= 0 ? 0 : (int)Math.Min(wanted, _items.Count - start);
        AsVector<T> removed = new AsVector<T>();
        removed._items.AddRange(_items.GetRange(start, remove));
        _items.RemoveRange(start, remove);
        _items.InsertRange(start, values);
        return removed;
    }

    public AsVector<T> Slice(double startIndex = 0, double endIndex = 16777215)
    {
        int start = Resolve(startIndex, _items.Count);
        int end = Resolve(endIndex, _items.Count);
        AsVector<T> result = new AsVector<T>();
        if (end > start)
        {
            result._items.AddRange(_items.GetRange(start, end - start));
        }
        return result;
    }

    public AsVector<T> Concat(params AsVector<T>[] others)
    {
        AsVector<T> result = new AsVector<T>();
        result._items.AddRange(_items);
        foreach (AsVector<T> other in others)
        {
            result._items.AddRange(other._items);
        }
        return result;
    }

    public string Join(string separator = ",")
    {
        StringBuilder builder = new StringBuilder();
        for (int i = 0; i < _items.Count; i++)
        {
            if (i > 0)
            {
                builder.Append(separator);
            }
            builder.Append(AsArray.JoinText(_items[i]));
        }
        return builder.ToString();
    }

    public AsVector<T> Reverse()
    {
        _items.Reverse();
        return this;
    }

    public AsVector<T> Sort(Delegate compare)
    {
        int[] order = new int[_items.Count];
        for (int i = 0; i < order.Length; i++)
        {
            order[i] = i;
        }
        List<T> snapshot = new List<T>(_items);
        Array.Sort(order, (x, y) =>
        {
            double outcome = Ops.ToNumber(Ops.Invoke(compare, snapshot[x], snapshot[y]));
            int result = outcome < 0 ? -1 : outcome > 0 ? 1 : 0;
            return result != 0 ? result : x.CompareTo(y);
        });
        for (int i = 0; i < order.Length; i++)
        {
            _items[i] = snapshot[order[i]];
        }
        return this;
    }

    public int IndexOf(T item, double fromIndex = 0)
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

    public int LastIndexOf(T item, double fromIndex = 2147483647)
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

    public AsVector<T> Filter(Delegate callback, object? thisObject = null)
    {
        AsVector<T> result = new AsVector<T>();
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

    public override string ToString() => Join();

    public IEnumerator<T> GetEnumerator() => _items.GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}
