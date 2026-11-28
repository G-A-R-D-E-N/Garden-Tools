using System;
using System.Numerics;

namespace BehaviourStudio.App;

internal readonly struct ViewportCamera
{
    public const double FitSpan = 2.6;

    public ViewportCamera(double yaw, double pitch, double zoom, double panX, double panY,
                          Vector3 centre, double fit)
    {
        Yaw = yaw;
        Pitch = pitch;
        Zoom = zoom;
        PanX = panX;
        PanY = panY;
        Centre = centre;
        Fit = Math.Max(0.0001, fit);
    }

    public double Yaw { get; }
    public double Pitch { get; }
    public double Zoom { get; }
    public double PanX { get; }
    public double PanY { get; }
    public Vector3 Centre { get; }
    public double Fit { get; }

    public double Scale(double width, double height) =>
        Math.Min(width, height) / (Fit * FitSpan) * Zoom;

    public (double X, double Y) Project(Vector3 world, double width, double height)
    {
        var p = world - Centre;

        double ca = Math.Cos(Yaw), sa = Math.Sin(Yaw);
        double x = p.X * ca - p.Y * sa;
        double depth = p.X * sa + p.Y * ca;

        double cp = Math.Cos(Pitch), sp = Math.Sin(Pitch);
        double up = p.Z * cp - depth * sp;

        double scale = Scale(width, height);
        return (width / 2 + x * scale + PanX, height / 2 - up * scale + PanY);
    }

    public Matrix4x4 View()
    {
        double ca = Math.Cos(Yaw), sa = Math.Sin(Yaw);
        double cp = Math.Cos(Pitch), sp = Math.Sin(Pitch);

        var view = new Matrix4x4(
            (float)ca, (float)(sp * sa), (float)(cp * sa), 0,
            (float)-sa, (float)(sp * ca), (float)(cp * ca), 0,
            0, (float)-cp, (float)sp, 0,
            0, 0, 0, 1);

        view.M41 = (float)(-Centre.X * ca + Centre.Y * sa);
        view.M42 = (float)(-Centre.X * sp * sa - Centre.Y * sp * ca + Centre.Z * cp);
        view.M43 = (float)(-Centre.X * cp * sa - Centre.Y * cp * ca - Centre.Z * sp);
        return view;
    }

    public Matrix4x4 Projection(double width, double height)
    {
        double scale = Scale(width, height);
        double left = -(width / 2 + PanX) / scale;
        double right = (width / 2 - PanX) / scale;
        double bottom = -(height / 2 + PanY) / scale;
        double top = (height / 2 - PanY) / scale;

        double depth = Math.Max(1, Fit * 32);
        double near = -depth, far = depth;

        var projection = Matrix4x4.Identity;
        projection.M11 = (float)(2 / (right - left));
        projection.M22 = (float)(-2 / (top - bottom));
        projection.M33 = (float)(2 / (far - near));
        projection.M41 = (float)(-(right + left) / (right - left));
        projection.M42 = (float)((top + bottom) / (top - bottom));
        projection.M43 = (float)(-(far + near) / (far - near));
        projection.M44 = 1;
        return projection;
    }

    public Matrix4x4 ViewProjection(double width, double height) =>
        View() * Projection(width, height);

    public (double X, double Y) ProjectThroughMatrix(Vector3 world, double width, double height)
    {
        var clip = Vector4.Transform(new Vector4(world, 1f), ViewProjection(width, height));
        double w = Math.Abs(clip.W) < 0.000001 ? 1 : clip.W;
        double ndcX = clip.X / w;
        double ndcY = clip.Y / w;
        return ((ndcX + 1) * width / 2, (1 - ndcY) * height / 2);
    }
}
