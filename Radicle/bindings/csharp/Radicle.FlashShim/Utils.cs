using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using Flash.Events;
using Store = System.Collections.Generic.Dictionary<object, object?>;

namespace Flash.Utils;

public static class Scheduler
{
    private sealed class Entry
    {
        public Entry(int id, double due, double interval, bool repeat, Action callback)
        {
            Id = id;
            Due = due;
            Interval = interval;
            Repeat = repeat;
            Callback = callback;
        }

        public int Id { get; }

        public double Due { get; set; }

        public double Interval { get; }

        public bool Repeat { get; }

        public Action Callback { get; }
    }

    private static readonly object Gate = new object();
    private static readonly List<Entry> Entries = new List<Entry>();
    private static readonly long Origin = Stopwatch.GetTimestamp();
    private static int _nextId = 1;

    public static Func<double> Clock { get; set; } = DefaultClock;

    public static double Now => Clock();

    private static double DefaultClock() => (Stopwatch.GetTimestamp() - Origin) * 1000.0 / Stopwatch.Frequency;

    public static int Schedule(Action callback, double delayMs, bool repeat)
    {
        double delay = double.IsNaN(delayMs) || delayMs < 1 ? 1 : delayMs;
        double due = Now + delay;
        lock (Gate)
        {
            int id = _nextId++;
            Entries.Add(new Entry(id, due, delay, repeat, callback));
            return id;
        }
    }

    public static void Cancel(int id)
    {
        lock (Gate)
        {
            Entries.RemoveAll(entry => entry.Id == id);
        }
    }

    public static int Pump()
    {
        double now = Now;
        List<Entry> due;
        lock (Gate)
        {
            due = Entries.FindAll(entry => entry.Due <= now);
        }
        due.Sort((a, b) =>
        {
            int order = a.Due.CompareTo(b.Due);
            return order != 0 ? order : a.Id.CompareTo(b.Id);
        });
        int fired = 0;
        foreach (Entry entry in due)
        {
            lock (Gate)
            {
                if (!Entries.Contains(entry))
                {
                    continue;
                }
                if (entry.Repeat)
                {
                    entry.Due = now + entry.Interval;
                }
                else
                {
                    Entries.Remove(entry);
                }
            }
            entry.Callback();
            fired++;
        }
        return fired;
    }

    public static void Reset()
    {
        lock (Gate)
        {
            Entries.Clear();
            _nextId = 1;
        }
        Clock = DefaultClock;
    }
}

public class Timer : EventDispatcher
{
    private double _delay;
    private int _scheduled;

    public Timer(double delay, int repeatCount = 0)
    {
        Delay = delay;
        RepeatCount = repeatCount;
    }

    public double Delay
    {
        get => _delay;
        set
        {
            if (double.IsNaN(value) || value < 0)
            {
                throw new ArgumentOutOfRangeException(nameof(value));
            }
            _delay = value;
            if (Running)
            {
                Scheduler.Cancel(_scheduled);
                _scheduled = Scheduler.Schedule(Tick, _delay, true);
            }
        }
    }

    public int RepeatCount { get; set; }

    public bool Running { get; private set; }

    public int CurrentCount { get; private set; }

    public void Start()
    {
        if (Running)
        {
            return;
        }
        Running = true;
        _scheduled = Scheduler.Schedule(Tick, _delay, true);
    }

    public void Stop()
    {
        if (!Running)
        {
            return;
        }
        Running = false;
        Scheduler.Cancel(_scheduled);
    }

    public void Reset()
    {
        Stop();
        CurrentCount = 0;
    }

    private void Tick()
    {
        CurrentCount++;
        DispatchEvent(new TimerEvent(TimerEvent.TIMER));
        if (RepeatCount > 0 && CurrentCount >= RepeatCount && Running)
        {
            Stop();
            DispatchEvent(new TimerEvent(TimerEvent.TIMER_COMPLETE));
        }
    }
}

public class Dictionary : IEnumerable<KeyValuePair<object?, object?>>
{
    private sealed class StrictComparer : IEqualityComparer<object>
    {
        public new bool Equals(object? x, object? y) => Ops.StrictEquals(x, y);

        public int GetHashCode(object value)
        {
            if (Ops.IsNumeric(value))
            {
                return Ops.ToNumber(value).GetHashCode();
            }
            return value is string || value is bool ? value.GetHashCode() : RuntimeHelpers.GetHashCode(value);
        }
    }

    private static readonly object NullKey = new object();

    private readonly Store _items = new Store(new StrictComparer());

    public Dictionary(bool weakKeys = false)
    {
        WeakKeys = weakKeys;
    }

    public bool WeakKeys { get; }

    public int Count => _items.Count;

    public object? this[object? key]
    {
        get => _items.TryGetValue(key ?? NullKey, out object? value) ? value : null;
        set => _items[key ?? NullKey] = value;
    }

    public bool HasOwnProperty(object? key) => _items.ContainsKey(key ?? NullKey);

    public bool Remove(object? key) => _items.Remove(key ?? NullKey);

    public IEnumerable<object?> Keys
    {
        get
        {
            foreach (object key in _items.Keys)
            {
                yield return ReferenceEquals(key, NullKey) ? null : key;
            }
        }
    }

    public IEnumerator<KeyValuePair<object?, object?>> GetEnumerator()
    {
        foreach (KeyValuePair<object, object?> pair in _items)
        {
            yield return new KeyValuePair<object?, object?>(ReferenceEquals(pair.Key, NullKey) ? null : pair.Key,
                                                            pair.Value);
        }
    }

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}

public class ByteArray
{
    private readonly List<byte> _data = new List<byte>();

    public ByteArray()
    {
    }

    public ByteArray(byte[] data)
    {
        _data.AddRange(data);
    }

    public uint Length
    {
        get => (uint)_data.Count;
        set
        {
            int length = (int)value;
            if (length < _data.Count)
            {
                _data.RemoveRange(length, _data.Count - length);
            }
            else
            {
                _data.AddRange(new byte[length - _data.Count]);
            }
        }
    }

    public uint Position { get; set; }

    public uint BytesAvailable => Position >= Length ? 0 : Length - Position;

    public byte[] ToArray() => _data.ToArray();
}
