using System;
using System.Collections.Generic;
using System.Threading;
using Flash.Events;
using Flash.Geom;

namespace Flash.Display;

public class DisplayObject : EventDispatcher
{
    private static int _nextInstance;

    private double _scaleX = 1;
    private double _scaleY = 1;

    public DisplayObject() : base(null)
    {
        Name = "instance" + Interlocked.Increment(ref _nextInstance);
    }

    public double X { get; set; }

    public double Y { get; set; }

    public double Width { get; set; }

    public double Height { get; set; }

    public double ScaleX
    {
        get => _scaleX;
        set => _scaleX = value;
    }

    public double ScaleY
    {
        get => _scaleY;
        set => _scaleY = value;
    }

    public double Rotation { get; set; }

    public double Alpha { get; set; } = 1;

    public bool Visible { get; set; } = true;

    public string Name { get; set; }

    public DisplayObjectContainer? Parent { get; internal set; }

    public DisplayObject? Mask { get; set; }

    public string BlendMode { get; set; } = "normal";

    public bool CacheAsBitmap { get; set; }

    public AsArray Filters { get; set; } = new AsArray();

    public Transform Transform { get; set; } = new Transform();

    public LoaderInfo? LoaderInfo { get; set; }

    internal virtual bool IsStage => false;

    internal override EventDispatcher? EventParent => Parent;

    public Stage? Stage
    {
        get
        {
            for (DisplayObject? node = this; node != null; node = node.Parent)
            {
                if (node.IsStage)
                {
                    return (Flash.Display.Stage)node;
                }
            }
            return null;
        }
    }

    public DisplayObject? Root
    {
        get
        {
            if (Stage == null || IsStage)
            {
                return null;
            }
            DisplayObject node = this;
            while (node.Parent != null && !node.Parent.IsStage)
            {
                node = node.Parent;
            }
            return node;
        }
    }

    public double MouseX => LocalMouse().X;

    public double MouseY => LocalMouse().Y;

    private Point LocalMouse()
    {
        Stage? stage = Stage;
        if (stage == null)
        {
            return new Point();
        }
        return GlobalToLocal(new Point(stage.MouseGlobalX, stage.MouseGlobalY));
    }

    internal Matrix LocalMatrix()
    {
        Matrix matrix = new Matrix();
        matrix.CreateBox(_scaleX, _scaleY, Rotation * Math.PI / 180, X, Y);
        return matrix;
    }

    public virtual Point LocalToGlobal(Point point)
    {
        Point inParent = LocalMatrix().TransformPoint(point);
        return Parent == null ? inParent : Parent.LocalToGlobal(inParent);
    }

    public virtual Point GlobalToLocal(Point point)
    {
        Point inParent = Parent == null ? point.Clone() : Parent.GlobalToLocal(point);
        Matrix inverse = LocalMatrix();
        if (!inverse.TryInvert())
        {
            return new Point(double.NaN, double.NaN);
        }
        return inverse.TransformPoint(inParent);
    }

    public virtual Rectangle GetBounds(DisplayObject? targetCoordinateSpace)
    {
        Point[] corners =
        {
            new Point(0, 0),
            new Point(Width, 0),
            new Point(0, Height),
            new Point(Width, Height),
        };
        double minX = double.PositiveInfinity;
        double minY = double.PositiveInfinity;
        double maxX = double.NegativeInfinity;
        double maxY = double.NegativeInfinity;
        foreach (Point corner in corners)
        {
            Point mapped = targetCoordinateSpace == this ? corner : LocalToGlobal(corner);
            if (targetCoordinateSpace != null && targetCoordinateSpace != this)
            {
                mapped = targetCoordinateSpace.GlobalToLocal(mapped);
            }
            minX = Math.Min(minX, mapped.X);
            minY = Math.Min(minY, mapped.Y);
            maxX = Math.Max(maxX, mapped.X);
            maxY = Math.Max(maxY, mapped.Y);
        }
        return new Rectangle(minX, minY, maxX - minX, maxY - minY);
    }

    public virtual Rectangle GetRect(DisplayObject? targetCoordinateSpace) => GetBounds(targetCoordinateSpace);

    public virtual bool HitTestObject(DisplayObject obj) => GetBounds(null).Intersects(obj.GetBounds(null));

    public virtual bool HitTestPoint(double x, double y, bool shapeFlag = false)
    {
        Point local = GlobalToLocal(new Point(x, y));
        return local.X >= 0 && local.X < Width && local.Y >= 0 && local.Y < Height;
    }
}

public class InteractiveObject : DisplayObject
{
    public bool MouseEnabled { get; set; } = true;

    public bool TabEnabled { get; set; }

    public int TabIndex { get; set; } = -1;

    public object? FocusRect { get; set; }

    public bool DoubleClickEnabled { get; set; }

    public bool Enabled { get; set; } = true;
}

public class DisplayObjectContainer : InteractiveObject
{
    private readonly List<DisplayObject> _children = new List<DisplayObject>();

    public bool MouseChildren { get; set; } = true;

    public int NumChildren => _children.Count;

    public virtual DisplayObject AddChild(DisplayObject child) =>
        AddChildAt(child, child.Parent == this ? _children.Count - 1 : _children.Count);

    public virtual DisplayObject AddChildAt(DisplayObject child, int index)
    {
        for (DisplayObjectContainer? node = this; node != null; node = node.Parent)
        {
            if (node == child)
            {
                throw new ArgumentException("A display object cannot contain itself or its ancestors.");
            }
        }
        if (child.Parent == this)
        {
            _children.Remove(child);
            if (index < 0 || index > _children.Count)
            {
                _children.Add(child);
                throw new ArgumentOutOfRangeException(nameof(index));
            }
            _children.Insert(index, child);
            return child;
        }
        if (index < 0 || index > _children.Count)
        {
            throw new ArgumentOutOfRangeException(nameof(index));
        }
        child.Parent?.RemoveChild(child);
        _children.Insert(index, child);
        child.Parent = this;
        child.DispatchEvent(new Event(Event.ADDED, true));
        if (Stage != null)
        {
            DispatchTree(child, Event.ADDED_TO_STAGE);
        }
        return child;
    }

    public virtual DisplayObject RemoveChild(DisplayObject child)
    {
        int index = _children.IndexOf(child);
        if (index < 0)
        {
            throw new ArgumentException("The supplied DisplayObject must be a child of the caller.");
        }
        return RemoveChildAt(index);
    }

    public virtual DisplayObject RemoveChildAt(int index)
    {
        if (index < 0 || index >= _children.Count)
        {
            throw new ArgumentOutOfRangeException(nameof(index));
        }
        DisplayObject child = _children[index];
        bool onStage = Stage != null;
        child.DispatchEvent(new Event(Event.REMOVED, true));
        if (onStage)
        {
            DispatchTree(child, Event.REMOVED_FROM_STAGE);
        }
        _children.Remove(child);
        child.Parent = null;
        return child;
    }

    public virtual void RemoveChildren(int beginIndex = 0, int endIndex = int.MaxValue)
    {
        if (_children.Count == 0)
        {
            return;
        }
        int last = endIndex == int.MaxValue ? _children.Count - 1 : endIndex;
        if (beginIndex < 0 || beginIndex >= _children.Count || last < beginIndex || last >= _children.Count)
        {
            throw new ArgumentOutOfRangeException(nameof(beginIndex));
        }
        for (int remaining = last - beginIndex + 1; remaining > 0; remaining--)
        {
            RemoveChildAt(beginIndex);
        }
    }

    public virtual DisplayObject GetChildAt(int index)
    {
        if (index < 0 || index >= _children.Count)
        {
            throw new ArgumentOutOfRangeException(nameof(index));
        }
        return _children[index];
    }

    public virtual DisplayObject? GetChildByName(string name)
    {
        foreach (DisplayObject child in _children)
        {
            if (child.Name == name)
            {
                return child;
            }
        }
        return null;
    }

    public virtual int GetChildIndex(DisplayObject child)
    {
        int index = _children.IndexOf(child);
        if (index < 0)
        {
            throw new ArgumentException("The supplied DisplayObject must be a child of the caller.");
        }
        return index;
    }

    public virtual void SetChildIndex(DisplayObject child, int index)
    {
        int current = GetChildIndex(child);
        if (index < 0 || index >= _children.Count)
        {
            throw new ArgumentOutOfRangeException(nameof(index));
        }
        _children.RemoveAt(current);
        _children.Insert(index, child);
    }

    public virtual void SwapChildren(DisplayObject child1, DisplayObject child2)
    {
        SwapChildrenAt(GetChildIndex(child1), GetChildIndex(child2));
    }

    public virtual void SwapChildrenAt(int index1, int index2)
    {
        DisplayObject first = GetChildAt(index1);
        DisplayObject second = GetChildAt(index2);
        _children[index1] = second;
        _children[index2] = first;
    }

    public virtual bool Contains(DisplayObject child)
    {
        for (DisplayObject? node = child; node != null; node = node.Parent)
        {
            if (node == this)
            {
                return true;
            }
        }
        return false;
    }

    public override Rectangle GetBounds(DisplayObject? targetCoordinateSpace)
    {
        Rectangle result = base.GetBounds(targetCoordinateSpace);
        foreach (DisplayObject child in _children)
        {
            result = result.Union(child.GetBounds(targetCoordinateSpace));
        }
        return result;
    }

    public override bool HitTestPoint(double x, double y, bool shapeFlag = false)
    {
        if (base.HitTestPoint(x, y, shapeFlag))
        {
            return true;
        }
        foreach (DisplayObject child in _children)
        {
            if (child.Visible && child.HitTestPoint(x, y, shapeFlag))
            {
                return true;
            }
        }
        return false;
    }

    private static void DispatchTree(DisplayObject node, string type)
    {
        node.DispatchEvent(new Event(type));
        if (node is DisplayObjectContainer container)
        {
            foreach (DisplayObject child in container._children.ToArray())
            {
                DispatchTree(child, type);
            }
        }
    }
}
