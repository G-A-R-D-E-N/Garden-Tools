using System;
using System.Collections;
using System.Collections.Generic;
using System.Dynamic;

namespace Flash;

public class AsObject : DynamicObject, IEnumerable<KeyValuePair<string, object?>>
{
    private readonly Dictionary<string, object?> _items = new Dictionary<string, object?>();

    public object? this[object? key]
    {
        get => _items.TryGetValue(Ops.ToString(key), out object? value) ? value : null;
        set => _items[Ops.ToString(key)] = value;
    }

    public int Count => _items.Count;

    public IEnumerable<string> Keys => _items.Keys;

    public bool HasOwnProperty(object? name) => _items.ContainsKey(Ops.ToString(name));

    public bool Remove(object? key) => _items.Remove(Ops.ToString(key));

    public object? ValueOf() => this;

    public override string ToString() => "[object Object]";

    public override bool TryGetMember(GetMemberBinder binder, out object? result)
    {
        _items.TryGetValue(binder.Name, out result);
        return true;
    }

    public override bool TrySetMember(SetMemberBinder binder, object? value)
    {
        _items[binder.Name] = value;
        return true;
    }

    public override bool TryInvokeMember(InvokeMemberBinder binder, object?[]? args, out object? result)
    {
        if (_items.TryGetValue(binder.Name, out object? member) && member is Delegate callback)
        {
            result = Ops.Invoke(callback, args ?? Array.Empty<object?>());
            return true;
        }
        result = null;
        return false;
    }

    public IEnumerator<KeyValuePair<string, object?>> GetEnumerator() =>
        ((IEnumerable<KeyValuePair<string, object?>>)_items).GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}
