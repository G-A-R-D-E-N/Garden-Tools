using System;
using Flash.Geom;
using Xunit;

namespace Radicle.Interop.Tests;

public class FlashGeomTests
{
    private const int Digits = 10;

    private static void AssertPoint(double x, double y, Point actual)
    {
        Assert.Equal(x, actual.X, Digits);
        Assert.Equal(y, actual.Y, Digits);
    }

    private static void AssertMatrix(double a, double b, double c, double d, double tx, double ty, Matrix m)
    {
        Assert.Equal(a, m.A, Digits);
        Assert.Equal(b, m.B, Digits);
        Assert.Equal(c, m.C, Digits);
        Assert.Equal(d, m.D, Digits);
        Assert.Equal(tx, m.Tx, Digits);
        Assert.Equal(ty, m.Ty, Digits);
    }

    [Fact]
    public void PointLengthAddSubtractAndClone()
    {
        Point p = new Point(3, 4);
        Assert.Equal(5.0, p.Length);
        AssertPoint(4, 6, new Point(1, 2).Add(new Point(3, 4)));
        AssertPoint(2, 2, new Point(3, 4).Subtract(new Point(1, 2)));
        Point copy = p.Clone();
        copy.X = 99;
        Assert.Equal(3.0, p.X);
        Assert.True(p.Equals(new Point(3, 4)));
        Assert.False(p.Equals(new Point(3, 5)));
        Assert.False(p.Equals(null));
    }

    [Fact]
    public void PointNormalizeScalesToThicknessAndLeavesZeroAlone()
    {
        Point p = new Point(3, 4);
        p.Normalize(10);
        AssertPoint(6, 8, p);
        Point unit = new Point(0, 5);
        unit.Normalize();
        AssertPoint(0, 1, unit);
        Point zero = new Point(0, 0);
        zero.Normalize(7);
        AssertPoint(0, 0, zero);
    }

    [Fact]
    public void PointOffsetDistanceInterpolateAndPolar()
    {
        Point p = new Point(1, 1);
        p.Offset(2, -3);
        AssertPoint(3, -2, p);
        Assert.Equal(5.0, Point.Distance(new Point(0, 0), new Point(3, 4)));
        Point from = new Point(0, 0);
        Point to = new Point(10, 20);
        AssertPoint(7.5, 15, Point.Interpolate(from, to, 0.25));
        AssertPoint(10, 20, Point.Interpolate(from, to, 0));
        AssertPoint(0, 0, Point.Interpolate(from, to, 1));
        AssertPoint(2, 0, Point.Polar(2, 0));
        AssertPoint(0, 1, Point.Polar(1, Math.PI / 2));
    }

    [Fact]
    public void PointToStringUsesActionScriptFormat()
    {
        Assert.Equal("(x=1.5, y=-2)", new Point(1.5, -2).ToString());
        Assert.Equal("(x=0, y=0)", new Point().ToString());
    }

    [Fact]
    public void RectangleEdgesAndCorners()
    {
        Rectangle r = new Rectangle(10, 20, 30, 40);
        Assert.Equal(10.0, r.Left);
        Assert.Equal(20.0, r.Top);
        Assert.Equal(40.0, r.Right);
        Assert.Equal(60.0, r.Bottom);
        AssertPoint(10, 20, r.TopLeft);
        AssertPoint(40, 60, r.BottomRight);
    }

    [Fact]
    public void RectangleLeftAndTopKeepTheOppositeEdge()
    {
        Rectangle r = new Rectangle(10, 20, 30, 40);
        r.Left = 0;
        Assert.Equal(0.0, r.X);
        Assert.Equal(40.0, r.Width);
        Assert.Equal(40.0, r.Right);
        r.Top = 10;
        Assert.Equal(50.0, r.Height);
        Assert.Equal(60.0, r.Bottom);
    }

    [Fact]
    public void RectangleRightAndBottomChangeTheSize()
    {
        Rectangle r = new Rectangle(10, 20, 30, 40);
        r.Right = 100;
        r.Bottom = 120;
        Assert.Equal(10.0, r.X);
        Assert.Equal(90.0, r.Width);
        Assert.Equal(100.0, r.Height);
        r.TopLeft = new Point(20, 30);
        Assert.Equal(80.0, r.Width);
        Assert.Equal(90.0, r.Height);
        r.BottomRight = new Point(50, 60);
        Assert.Equal(30.0, r.Width);
        Assert.Equal(30.0, r.Height);
    }

    [Fact]
    public void RectangleEmptiness()
    {
        Assert.True(new Rectangle().IsEmpty());
        Assert.True(new Rectangle(0, 0, 0, 5).IsEmpty());
        Assert.True(new Rectangle(0, 0, -3, 5).IsEmpty());
        Assert.False(new Rectangle(0, 0, 5, 5).IsEmpty());
        Rectangle r = new Rectangle(1, 2, 3, 4);
        r.SetEmpty();
        Assert.True(r.IsEmpty());
        Assert.Equal(0.0, r.X);
        Assert.Equal(0.0, r.Height);
    }

    [Fact]
    public void RectangleContainsExcludesTheFarEdges()
    {
        Rectangle r = new Rectangle(0, 0, 10, 10);
        Assert.True(r.Contains(0, 0));
        Assert.True(r.Contains(9.9, 9.9));
        Assert.False(r.Contains(10, 10));
        Assert.False(r.Contains(-1, 5));
        Assert.True(r.ContainsPoint(new Point(5, 5)));
        Assert.False(r.ContainsPoint(new Point(5, 10)));
    }

    [Fact]
    public void RectangleContainsRect()
    {
        Rectangle outer = new Rectangle(0, 0, 10, 10);
        Assert.True(outer.ContainsRect(new Rectangle(2, 2, 3, 3)));
        Assert.True(outer.ContainsRect(outer.Clone()));
        Assert.False(outer.ContainsRect(new Rectangle(5, 5, 10, 10)));
    }

    [Fact]
    public void RectangleIntersectsAndIntersection()
    {
        Rectangle a = new Rectangle(0, 0, 10, 10);
        Assert.True(a.Intersects(new Rectangle(5, 5, 10, 10)));
        Assert.False(a.Intersects(new Rectangle(10, 0, 5, 5)));
        Assert.False(a.Intersects(new Rectangle(5, 5, 0, 0)));
        Rectangle overlap = a.Intersection(new Rectangle(5, 5, 10, 10));
        Assert.True(overlap.Equals(new Rectangle(5, 5, 5, 5)));
        Rectangle none = a.Intersection(new Rectangle(50, 50, 1, 1));
        Assert.True(none.IsEmpty());
        Assert.Equal(0.0, none.X);
    }

    [Fact]
    public void RectangleUnionSkipsEmptyOperandsAndClones()
    {
        Rectangle a = new Rectangle(0, 0, 10, 10);
        Assert.True(a.Union(new Rectangle(20, 20, 5, 5)).Equals(new Rectangle(0, 0, 25, 25)));
        Rectangle fromEmpty = new Rectangle().Union(a);
        Assert.True(fromEmpty.Equals(a));
        Assert.NotSame(a, fromEmpty);
        Assert.True(a.Union(new Rectangle()).Equals(a));
    }

    [Fact]
    public void RectangleInflateOffsetEqualsAndToString()
    {
        Rectangle r = new Rectangle(10, 10, 10, 10);
        r.Inflate(2, 3);
        Assert.True(r.Equals(new Rectangle(8, 7, 14, 16)));
        r.InflatePoint(new Point(-2, -3));
        Assert.True(r.Equals(new Rectangle(10, 10, 10, 10)));
        r.Offset(5, -5);
        Assert.True(r.Equals(new Rectangle(15, 5, 10, 10)));
        r.OffsetPoint(new Point(-5, 5));
        Assert.True(r.Equals(new Rectangle(10, 10, 10, 10)));
        Assert.False(r.Equals(null));
        Assert.Equal("(x=1, y=2, w=3, h=4)", new Rectangle(1, 2, 3, 4).ToString());
    }

    [Fact]
    public void MatrixDefaultsToIdentity()
    {
        Matrix m = new Matrix();
        AssertMatrix(1, 0, 0, 1, 0, 0, m);
        AssertPoint(3, 4, m.TransformPoint(new Point(3, 4)));
        Assert.Equal("(a=1, b=0, c=0, d=1, tx=0, ty=0)", m.ToString());
    }

    [Fact]
    public void MatrixTranslateScaleAndRotate()
    {
        Matrix t = new Matrix();
        t.Translate(5, 6);
        AssertPoint(5, 6, t.TransformPoint(new Point(0, 0)));
        Matrix s = new Matrix();
        s.Scale(2, 3);
        AssertMatrix(2, 0, 0, 3, 0, 0, s);
        Matrix both = new Matrix();
        both.Translate(10, 10);
        both.Scale(2, 2);
        AssertMatrix(2, 0, 0, 2, 20, 20, both);
        Matrix r = new Matrix();
        r.Rotate(Math.PI / 2);
        AssertMatrix(0, 1, -1, 0, 0, 0, r);
        AssertPoint(0, 1, r.TransformPoint(new Point(1, 0)));
    }

    [Fact]
    public void MatrixConcatAppliesTheReceiverFirst()
    {
        Matrix translateThenScale = new Matrix(1, 0, 0, 1, 10, 0);
        translateThenScale.Concat(new Matrix(2, 0, 0, 2, 0, 0));
        AssertPoint(20, 0, translateThenScale.TransformPoint(new Point(0, 0)));
        Matrix scaleThenTranslate = new Matrix(2, 0, 0, 2, 0, 0);
        scaleThenTranslate.Concat(new Matrix(1, 0, 0, 1, 10, 0));
        AssertPoint(10, 0, scaleThenTranslate.TransformPoint(new Point(0, 0)));
    }

    [Fact]
    public void MatrixDeltaTransformIgnoresTranslation()
    {
        Matrix m = new Matrix(2, 0, 0, 3, 100, 100);
        AssertPoint(2, 3, m.DeltaTransformPoint(new Point(1, 1)));
        AssertPoint(102, 103, m.TransformPoint(new Point(1, 1)));
    }

    [Fact]
    public void MatrixInvertRoundTripsAndLeavesSingularMatricesUnchanged()
    {
        Matrix m = new Matrix(2, 0, 0, 4, 10, 20);
        Matrix original = m.Clone();
        m.Invert();
        AssertPoint(-5, -5, m.TransformPoint(new Point(0, 0)));
        m.Concat(original);
        AssertMatrix(1, 0, 0, 1, 0, 0, m);

        Matrix singular = new Matrix(0, 0, 0, 0, 5, 5);
        Assert.False(singular.TryInvert());
        singular.Invert();
        AssertMatrix(0, 0, 0, 0, 5, 5, singular);

        Matrix skewed = new Matrix(1, 2, 3, 4, 5, 6);
        Matrix copy = skewed.Clone();
        Assert.True(skewed.TryInvert());
        skewed.Concat(copy);
        AssertMatrix(1, 0, 0, 1, 0, 0, skewed);
    }

    [Fact]
    public void MatrixCreateBoxCloneAndIdentity()
    {
        Matrix m = new Matrix();
        m.CreateBox(2, 3, 0, 5, 6);
        AssertMatrix(2, 0, 0, 3, 5, 6, m);
        m.CreateBox(1, 1, Math.PI / 2);
        AssertMatrix(0, 1, -1, 0, 0, 0, m);
        Matrix copy = m.Clone();
        copy.A = 42;
        Assert.NotEqual(42.0, m.A);
        copy.Identity();
        AssertMatrix(1, 0, 0, 1, 0, 0, copy);
    }

    [Fact]
    public void ColorTransformColorGetAndSet()
    {
        ColorTransform ct = new ColorTransform();
        Assert.Equal(0u, ct.Color);
        ct.Color = 0xFF8040;
        Assert.Equal(0.0, ct.RedMultiplier);
        Assert.Equal(0.0, ct.GreenMultiplier);
        Assert.Equal(0.0, ct.BlueMultiplier);
        Assert.Equal(1.0, ct.AlphaMultiplier);
        Assert.Equal(255.0, ct.RedOffset);
        Assert.Equal(128.0, ct.GreenOffset);
        Assert.Equal(64.0, ct.BlueOffset);
        Assert.Equal(0xFF8040u, ct.Color);
        ct.RedOffset = 300;
        ct.GreenOffset = -5;
        Assert.Equal(0xFF0040u, ct.Color);
    }

    [Fact]
    public void ColorTransformConcatMultipliesAndCarriesOffsets()
    {
        ColorTransform first = new ColorTransform(0.5, 0.5, 0.5, 1, 10, 10, 10, 0);
        first.Concat(new ColorTransform(2, 2, 2, 1, 5, 5, 5, 0));
        Assert.Equal(25.0, first.RedOffset);
        Assert.Equal(25.0, first.BlueOffset);
        Assert.Equal(1.0, first.RedMultiplier);
        Assert.Equal(1.0, first.AlphaMultiplier);
        Assert.Equal(0.0, first.AlphaOffset);
        Assert.Equal(
            "(redMultiplier=1, greenMultiplier=1, blueMultiplier=1, alphaMultiplier=1, redOffset=25, greenOffset=25, blueOffset=25, alphaOffset=0)",
            first.ToString());
    }

    [Fact]
    public void TransformHoldsAnIdentityMatrixAndColorTransform()
    {
        Transform t = new Transform();
        AssertMatrix(1, 0, 0, 1, 0, 0, t.Matrix);
        Assert.Equal(1.0, t.ColorTransform.RedMultiplier);
    }

    [Fact]
    public void Vector3DArithmetic()
    {
        Vector3D v = new Vector3D(1, 2, 2);
        Assert.Equal(3.0, v.Length);
        Assert.Equal(9.0, v.LengthSquared);
        Assert.Equal(11.0, v.DotProduct(new Vector3D(1, 2, 3)));
        Vector3D cross = new Vector3D(1, 0, 0).CrossProduct(new Vector3D(0, 1, 0));
        Assert.Equal(0.0, cross.X);
        Assert.Equal(0.0, cross.Y);
        Assert.Equal(1.0, cross.Z);
        Assert.Equal(1.0, cross.W);
        Vector3D sum = v.Add(new Vector3D(1, 1, 1));
        Assert.Equal(2.0, sum.X);
        Assert.Equal(3.0, sum.Y);
        Assert.Equal(3.0, sum.Z);
        Assert.Equal(3.0, Vector3D.Distance(new Vector3D(0, 0, 0), v));
    }

    [Fact]
    public void Vector3DNormalizeNegateScaleAndClone()
    {
        Vector3D v = new Vector3D(0, 3, 4);
        Assert.Equal(5.0, v.Normalize());
        Assert.Equal(0.6, v.Y, Digits);
        Assert.Equal(0.8, v.Z, Digits);
        v.Negate();
        Assert.Equal(-0.6, v.Y, Digits);
        v.Scale(5);
        Assert.Equal(-3.0, v.Y, Digits);
        Vector3D copy = v.Clone();
        copy.X = 9;
        Assert.Equal(0.0, v.X);
        Vector3D zero = new Vector3D();
        Assert.Equal(0.0, zero.Normalize());
    }
}
