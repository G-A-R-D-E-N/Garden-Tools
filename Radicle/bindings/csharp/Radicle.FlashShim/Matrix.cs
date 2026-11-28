using System;

namespace Flash.Geom;

public class Matrix
{
    public Matrix(double a = 1, double b = 0, double c = 0, double d = 1, double tx = 0, double ty = 0)
    {
        A = a;
        B = b;
        C = c;
        D = d;
        Tx = tx;
        Ty = ty;
    }

    public double A { get; set; }

    public double B { get; set; }

    public double C { get; set; }

    public double D { get; set; }

    public double Tx { get; set; }

    public double Ty { get; set; }

    public Matrix Clone() => new Matrix(A, B, C, D, Tx, Ty);

    public void Identity()
    {
        A = 1;
        B = 0;
        C = 0;
        D = 1;
        Tx = 0;
        Ty = 0;
    }

    public void Concat(Matrix m)
    {
        double a = A * m.A + B * m.C;
        double b = A * m.B + B * m.D;
        double c = C * m.A + D * m.C;
        double d = C * m.B + D * m.D;
        double tx = Tx * m.A + Ty * m.C + m.Tx;
        double ty = Tx * m.B + Ty * m.D + m.Ty;
        A = a;
        B = b;
        C = c;
        D = d;
        Tx = tx;
        Ty = ty;
    }

    public void Translate(double dx, double dy)
    {
        Tx += dx;
        Ty += dy;
    }

    public void Scale(double sx, double sy) => Concat(new Matrix(sx, 0, 0, sy, 0, 0));

    public void Rotate(double angle)
    {
        double cos = Math.Cos(angle);
        double sin = Math.Sin(angle);
        Concat(new Matrix(cos, sin, -sin, cos, 0, 0));
    }

    public Point TransformPoint(Point point) =>
        new Point(A * point.X + C * point.Y + Tx, B * point.X + D * point.Y + Ty);

    public Point DeltaTransformPoint(Point point) =>
        new Point(A * point.X + C * point.Y, B * point.X + D * point.Y);

    public bool TryInvert()
    {
        double det = A * D - B * C;
        if (det == 0 || double.IsNaN(det))
        {
            return false;
        }
        double a = D / det;
        double b = -B / det;
        double c = -C / det;
        double d = A / det;
        double tx = (C * Ty - D * Tx) / det;
        double ty = (B * Tx - A * Ty) / det;
        A = a;
        B = b;
        C = c;
        D = d;
        Tx = tx;
        Ty = ty;
        return true;
    }

    public void Invert() => TryInvert();

    public void CreateBox(double scaleX, double scaleY, double rotation = 0, double tx = 0, double ty = 0)
    {
        double cos = Math.Cos(rotation);
        double sin = Math.Sin(rotation);
        A = cos * scaleX;
        B = sin * scaleX;
        C = -sin * scaleY;
        D = cos * scaleY;
        Tx = tx;
        Ty = ty;
    }

    public override string ToString() =>
        "(a=" + Ops.ToString(A) + ", b=" + Ops.ToString(B) + ", c=" + Ops.ToString(C) + ", d=" + Ops.ToString(D) +
        ", tx=" + Ops.ToString(Tx) + ", ty=" + Ops.ToString(Ty) + ")";
}

public class ColorTransform
{
    public ColorTransform(double redMultiplier = 1, double greenMultiplier = 1, double blueMultiplier = 1,
                          double alphaMultiplier = 1, double redOffset = 0, double greenOffset = 0,
                          double blueOffset = 0, double alphaOffset = 0)
    {
        RedMultiplier = redMultiplier;
        GreenMultiplier = greenMultiplier;
        BlueMultiplier = blueMultiplier;
        AlphaMultiplier = alphaMultiplier;
        RedOffset = redOffset;
        GreenOffset = greenOffset;
        BlueOffset = blueOffset;
        AlphaOffset = alphaOffset;
    }

    public double RedMultiplier { get; set; }

    public double GreenMultiplier { get; set; }

    public double BlueMultiplier { get; set; }

    public double AlphaMultiplier { get; set; }

    public double RedOffset { get; set; }

    public double GreenOffset { get; set; }

    public double BlueOffset { get; set; }

    public double AlphaOffset { get; set; }

    public uint Color
    {
        get => (Channel(RedOffset) << 16) | (Channel(GreenOffset) << 8) | Channel(BlueOffset);
        set
        {
            RedMultiplier = 0;
            GreenMultiplier = 0;
            BlueMultiplier = 0;
            RedOffset = (value >> 16) & 0xFF;
            GreenOffset = (value >> 8) & 0xFF;
            BlueOffset = value & 0xFF;
        }
    }

    private static uint Channel(double offset)
    {
        if (double.IsNaN(offset))
        {
            return 0;
        }
        return (uint)Math.Min(255, Math.Max(0, Math.Truncate(offset)));
    }

    public void Concat(ColorTransform second)
    {
        RedOffset = RedOffset * second.RedMultiplier + second.RedOffset;
        GreenOffset = GreenOffset * second.GreenMultiplier + second.GreenOffset;
        BlueOffset = BlueOffset * second.BlueMultiplier + second.BlueOffset;
        AlphaOffset = AlphaOffset * second.AlphaMultiplier + second.AlphaOffset;
        RedMultiplier *= second.RedMultiplier;
        GreenMultiplier *= second.GreenMultiplier;
        BlueMultiplier *= second.BlueMultiplier;
        AlphaMultiplier *= second.AlphaMultiplier;
    }

    public override string ToString() =>
        "(redMultiplier=" + Ops.ToString(RedMultiplier) + ", greenMultiplier=" + Ops.ToString(GreenMultiplier) +
        ", blueMultiplier=" + Ops.ToString(BlueMultiplier) + ", alphaMultiplier=" + Ops.ToString(AlphaMultiplier) +
        ", redOffset=" + Ops.ToString(RedOffset) + ", greenOffset=" + Ops.ToString(GreenOffset) +
        ", blueOffset=" + Ops.ToString(BlueOffset) + ", alphaOffset=" + Ops.ToString(AlphaOffset) + ")";
}

public class Transform
{
    public Transform()
    {
        Matrix = new Matrix();
        ColorTransform = new ColorTransform();
    }

    public Matrix Matrix { get; set; }

    public ColorTransform ColorTransform { get; set; }
}

public class Vector3D
{
    public Vector3D(double x = 0, double y = 0, double z = 0, double w = 0)
    {
        X = x;
        Y = y;
        Z = z;
        W = w;
    }

    public double X { get; set; }

    public double Y { get; set; }

    public double Z { get; set; }

    public double W { get; set; }

    public double Length => Math.Sqrt(LengthSquared);

    public double LengthSquared => X * X + Y * Y + Z * Z;

    public Vector3D Add(Vector3D a) => new Vector3D(X + a.X, Y + a.Y, Z + a.Z);

    public Vector3D Subtract(Vector3D a) => new Vector3D(X - a.X, Y - a.Y, Z - a.Z);

    public double DotProduct(Vector3D a) => X * a.X + Y * a.Y + Z * a.Z;

    public Vector3D CrossProduct(Vector3D a) =>
        new Vector3D(Y * a.Z - Z * a.Y, Z * a.X - X * a.Z, X * a.Y - Y * a.X, 1);

    public double Normalize()
    {
        double length = Length;
        if (length != 0)
        {
            X /= length;
            Y /= length;
            Z /= length;
        }
        return length;
    }

    public Vector3D Clone() => new Vector3D(X, Y, Z, W);

    public void Scale(double s)
    {
        X *= s;
        Y *= s;
        Z *= s;
    }

    public void Negate()
    {
        X = -X;
        Y = -Y;
        Z = -Z;
    }

    public static double Distance(Vector3D pt1, Vector3D pt2) => pt1.Subtract(pt2).Length;

    public override string ToString() =>
        "Vector3D(" + Ops.ToString(X) + ", " + Ops.ToString(Y) + ", " + Ops.ToString(Z) + ")";
}
