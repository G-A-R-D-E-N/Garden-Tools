using System;

namespace Flash.Geom;

public class Point
{
    public Point(double x = 0, double y = 0)
    {
        X = x;
        Y = y;
    }

    public double X { get; set; }

    public double Y { get; set; }

    public double Length => Math.Sqrt(X * X + Y * Y);

    public Point Add(Point v) => new Point(X + v.X, Y + v.Y);

    public Point Subtract(Point v) => new Point(X - v.X, Y - v.Y);

    public Point Clone() => new Point(X, Y);

    public void Normalize(double thickness = 1)
    {
        double length = Length;
        if (length == 0)
        {
            return;
        }
        double scale = thickness / length;
        X *= scale;
        Y *= scale;
    }

    public void Offset(double dx, double dy)
    {
        X += dx;
        Y += dy;
    }

    public bool Equals(Point? toCompare) => toCompare != null && X == toCompare.X && Y == toCompare.Y;

    public static double Distance(Point pt1, Point pt2)
    {
        double dx = pt1.X - pt2.X;
        double dy = pt1.Y - pt2.Y;
        return Math.Sqrt(dx * dx + dy * dy);
    }

    public static Point Interpolate(Point pt1, Point pt2, double f) =>
        new Point(pt2.X + f * (pt1.X - pt2.X), pt2.Y + f * (pt1.Y - pt2.Y));

    public static Point Polar(double len, double angle) => new Point(len * Math.Cos(angle), len * Math.Sin(angle));

    public override string ToString() => "(x=" + Ops.ToString(X) + ", y=" + Ops.ToString(Y) + ")";
}

public class Rectangle
{
    public Rectangle(double x = 0, double y = 0, double width = 0, double height = 0)
    {
        X = x;
        Y = y;
        Width = width;
        Height = height;
    }

    public double X { get; set; }

    public double Y { get; set; }

    public double Width { get; set; }

    public double Height { get; set; }

    public double Left
    {
        get => X;
        set
        {
            Width += X - value;
            X = value;
        }
    }

    public double Top
    {
        get => Y;
        set
        {
            Height += Y - value;
            Y = value;
        }
    }

    public double Right
    {
        get => X + Width;
        set => Width = value - X;
    }

    public double Bottom
    {
        get => Y + Height;
        set => Height = value - Y;
    }

    public Point TopLeft
    {
        get => new Point(Left, Top);
        set
        {
            Left = value.X;
            Top = value.Y;
        }
    }

    public Point BottomRight
    {
        get => new Point(Right, Bottom);
        set
        {
            Right = value.X;
            Bottom = value.Y;
        }
    }

    public bool IsEmpty() => !(Width > 0) || !(Height > 0);

    public void SetEmpty()
    {
        X = 0;
        Y = 0;
        Width = 0;
        Height = 0;
    }

    public Rectangle Clone() => new Rectangle(X, Y, Width, Height);

    public bool Contains(double x, double y) => x >= X && x < X + Width && y >= Y && y < Y + Height;

    public bool ContainsPoint(Point point) => Contains(point.X, point.Y);

    public bool ContainsRect(Rectangle rect) =>
        rect.X >= X && rect.Y >= Y && rect.Right <= Right && rect.Bottom <= Bottom;

    public bool Intersects(Rectangle toIntersect)
    {
        if (IsEmpty() || toIntersect.IsEmpty())
        {
            return false;
        }
        return X < toIntersect.Right && toIntersect.X < Right && Y < toIntersect.Bottom && toIntersect.Y < Bottom;
    }

    public Rectangle Intersection(Rectangle toIntersect)
    {
        if (!Intersects(toIntersect))
        {
            return new Rectangle();
        }
        double left = Math.Max(X, toIntersect.X);
        double top = Math.Max(Y, toIntersect.Y);
        double right = Math.Min(Right, toIntersect.Right);
        double bottom = Math.Min(Bottom, toIntersect.Bottom);
        return new Rectangle(left, top, right - left, bottom - top);
    }

    public Rectangle Union(Rectangle toUnion)
    {
        if (IsEmpty())
        {
            return toUnion.Clone();
        }
        if (toUnion.IsEmpty())
        {
            return Clone();
        }
        double left = Math.Min(X, toUnion.X);
        double top = Math.Min(Y, toUnion.Y);
        double right = Math.Max(Right, toUnion.Right);
        double bottom = Math.Max(Bottom, toUnion.Bottom);
        return new Rectangle(left, top, right - left, bottom - top);
    }

    public void Inflate(double dx, double dy)
    {
        X -= dx;
        Width += 2 * dx;
        Y -= dy;
        Height += 2 * dy;
    }

    public void InflatePoint(Point point) => Inflate(point.X, point.Y);

    public void Offset(double dx, double dy)
    {
        X += dx;
        Y += dy;
    }

    public void OffsetPoint(Point point) => Offset(point.X, point.Y);

    public bool Equals(Rectangle? toCompare) =>
        toCompare != null && X == toCompare.X && Y == toCompare.Y && Width == toCompare.Width &&
        Height == toCompare.Height;

    public override string ToString() =>
        "(x=" + Ops.ToString(X) + ", y=" + Ops.ToString(Y) + ", w=" + Ops.ToString(Width) + ", h=" +
        Ops.ToString(Height) + ")";
}
