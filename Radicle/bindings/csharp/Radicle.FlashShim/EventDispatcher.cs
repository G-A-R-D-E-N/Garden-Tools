using System;
using System.Collections.Generic;

namespace Flash.Events;

public interface IEventDispatcher
{
    void AddEventListener(string type, Delegate listener, bool useCapture = false, int priority = 0,
                          bool useWeakReference = false);

    void RemoveEventListener(string type, Delegate listener, bool useCapture = false);

    bool DispatchEvent(Event e);

    bool HasEventListener(string type);

    bool WillTrigger(string type);
}

public class EventDispatcher : IEventDispatcher
{
    private sealed class Listener
    {
        public Listener(Delegate handler, bool capture, int priority)
        {
            Handler = handler;
            Capture = capture;
            Priority = priority;
        }

        public Delegate Handler { get; }

        public bool Capture { get; }

        public int Priority { get; }
    }

    private readonly Dictionary<string, List<Listener>> _listeners = new Dictionary<string, List<Listener>>();
    private readonly object _target;

    public EventDispatcher(IEventDispatcher? target = null)
    {
        _target = target ?? this;
    }

    internal virtual EventDispatcher? EventParent => null;

    public virtual void AddEventListener(string type, Delegate listener, bool useCapture = false, int priority = 0,
                                         bool useWeakReference = false)
    {
        if (!_listeners.TryGetValue(type, out List<Listener>? list))
        {
            list = new List<Listener>();
            _listeners[type] = list;
        }
        foreach (Listener existing in list)
        {
            if (existing.Capture == useCapture && existing.Handler.Equals(listener))
            {
                return;
            }
        }
        int at = list.Count;
        for (int i = 0; i < list.Count; i++)
        {
            if (list[i].Priority < priority)
            {
                at = i;
                break;
            }
        }
        list.Insert(at, new Listener(listener, useCapture, priority));
    }

    public virtual void RemoveEventListener(string type, Delegate listener, bool useCapture = false)
    {
        if (!_listeners.TryGetValue(type, out List<Listener>? list))
        {
            return;
        }
        list.RemoveAll(existing => existing.Capture == useCapture && existing.Handler.Equals(listener));
        if (list.Count == 0)
        {
            _listeners.Remove(type);
        }
    }

    public virtual bool HasEventListener(string type) => _listeners.ContainsKey(type);

    public virtual bool WillTrigger(string type)
    {
        for (EventDispatcher? node = this; node != null; node = node.EventParent)
        {
            if (node.HasEventListener(type))
            {
                return true;
            }
        }
        return false;
    }

    public virtual bool DispatchEvent(Event e)
    {
        e.Target = _target;
        e.StopAfterNode = false;
        e.StopNow = false;
        List<EventDispatcher> path = new List<EventDispatcher>();
        for (EventDispatcher? node = EventParent; node != null; node = node.EventParent)
        {
            path.Add(node);
        }
        for (int i = path.Count - 1; i >= 0 && !e.StopAfterNode; i--)
        {
            path[i].Notify(e, EventPhase.CAPTURING_PHASE, true);
        }
        if (!e.StopAfterNode)
        {
            Notify(e, EventPhase.AT_TARGET, false);
        }
        if (e.Bubbles)
        {
            for (int i = 0; i < path.Count && !e.StopAfterNode; i++)
            {
                path[i].Notify(e, EventPhase.BUBBLING_PHASE, false);
            }
        }
        return !e.IsDefaultPrevented();
    }

    private void Notify(Event e, uint phase, bool capture)
    {
        e.CurrentTarget = _target;
        e.EventPhase = phase;
        if (!_listeners.TryGetValue(e.Type, out List<Listener>? list))
        {
            return;
        }
        foreach (Listener listener in list.ToArray())
        {
            if (e.StopNow)
            {
                break;
            }
            if (listener.Capture == capture)
            {
                Ops.Invoke(listener.Handler, e);
            }
        }
    }
}
