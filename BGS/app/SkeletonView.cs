using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using OpenCommonwealth.Services.Hkx;
using Vector3 = System.Numerics.Vector3;

namespace BehaviourStudio.App;

public class SkeletonView : Control
{
    private AnimationPose.Pose? _pose;
    private AnimationPose.Pose? _reference;

    private double _yaw = 0.6;
    private double _pitch = 0.25;
    private double _zoom = 1;
    private Point _pan;
    private Point _lastPointer;
    private bool _orbiting;
    private bool _panning;

    private double _fit = 1;
    private Vector3 _centre;

    private HavokRagdollModel? _bodies;
    private Vector3 _bodyCentre;
    private float _bodyFit = 1;

    public string HoveredBone { get; private set; } = "";
    public Action<string>? BoneHovered;

    public bool ShowReference { get; set; }
    public bool ShowBodies { get; set; }

    public int DrawnBones => _pose?.Bones.Count ?? 0;
    public int DrawnEdges => _mesh?.Length ?? 0;
    public int DrawnBodies => _bodies?.Bodies.Count ?? 0;

    private (Vector3 A, Vector3 B)[]? _mesh;

    public void ShowMesh((Vector3 A, Vector3 B)[]? segments)
    {
        _mesh = segments;
        InvalidateVisual();
    }

    public void Show(AnimationPose.Pose? pose, AnimationPose.Pose? reference = null)
    {
        _pose = pose;
        _reference = reference;
        if (pose != null && pose.Bones.Count > 0)
        {
            _centre = pose.Centre;
            _fit = pose.Radius;
        }
        InvalidateVisual();
    }

    public void Update(AnimationPose.Pose? pose)
    {
        _pose = pose;
        InvalidateVisual();
    }

    public void Reset()
    {
        _pose = null;
        _reference = null;
        _mesh = null;
        _bodies = null;
        _bodyCentre = Vector3.Zero;
        _bodyFit = 1;
        ShowBodies = false;
        _yaw = 0.6;
        _pitch = 0.25;
        _zoom = 1;
        _pan = default;
        InvalidateVisual();
    }

    public void SetBodies(HavokRagdollModel? model)
    {
        _bodies = model;
        FitBodies();
        InvalidateVisual();
    }

    private void FitBodies()
    {
        _bodyCentre = Vector3.Zero;
        _bodyFit = 1;
        if (_bodies == null || _bodies.Bodies.Count == 0) return;

        var min = new Vector3(float.MaxValue);
        var max = new Vector3(float.MinValue);
        bool any = false;

        foreach (var body in _bodies.Bodies)
        {
            var shape = _bodies.Shapes.FirstOrDefault(s => s.Id == body.ShapeId);
            if (shape != null)
                foreach (var vertex in shape.Vertices)
                {
                    var world = body.Position + Vector3.Transform(vertex, body.Rotation);
                    min = Vector3.Min(min, world);
                    max = Vector3.Max(max, world);
                    any = true;
                }

            min = Vector3.Min(min, body.Position);
            max = Vector3.Max(max, body.Position);
            any = true;
        }

        if (!any) return;

        _bodyCentre = (min + max) * 0.5f;
        var size = max - min;
        _bodyFit = Math.Max(0.001f, Math.Max(size.X, Math.Max(size.Y, size.Z)) * 0.5f);
    }

    public void Frame()
    {
        _zoom = 1;
        _pan = default;
        if (_pose is { Bones.Count: > 0 })
        {
            _centre = _pose.Centre;
            _fit = _pose.Radius;
        }
        InvalidateVisual();
    }

    private Point Project(Vector3 world)
    {
        var p = world - _centre;

        double cy = Math.Cos(_yaw), sy = Math.Sin(_yaw);
        double x = p.X * cy - p.Y * sy;
        double depth = p.X * sy + p.Y * cy;

        double cp = Math.Cos(_pitch), sp = Math.Sin(_pitch);
        double up = p.Z * cp - depth * sp;

        double scale = Math.Min(Bounds.Width, Bounds.Height) / (_fit * 2.6) * _zoom;
        return new Point(Bounds.Width / 2 + x * scale + _pan.X,
                         Bounds.Height / 2 - up * scale + _pan.Y);
    }

    public override void Render(DrawingContext ctx)
    {
        ctx.FillRectangle(Ux.CardBrush, new Rect(Bounds.Size));

        var pose = _pose;
        bool bodies = ShowBodies && _bodies != null && _bodies.Bodies.Count > 0;
        if ((pose == null || pose.Bones.Count == 0) && !bodies)
        {
            var empty = new FormattedText("No pose to draw.", CultureInfo.InvariantCulture,
                                          FlowDirection.LeftToRight, Typeface.Default, 12, Ux.MutedBrush);
            ctx.DrawText(empty, new Point(14, 14));
            return;
        }

        if (pose == null || pose.Bones.Count == 0)
        {
            // A physics-only file can be framed by its rigid-body hull alone.
            _centre = _bodyCentre;
            _fit = _bodyFit;
        }

        DrawGround(ctx);

        if (ShowReference && _reference != null)
        {
            var ghost = new Pen(new SolidColorBrush(Ux.TextDisabled, 0.5), 1);
            foreach (var (from, to) in _reference.Links)
                ctx.DrawLine(ghost, Project(_reference.Bones[from].Position),
                                    Project(_reference.Bones[to].Position));
        }

        if (_mesh is { Length: > 0 })
        {
            var skin = new StreamGeometry();
            using (var draw = skin.Open())
                foreach (var (a, b) in _mesh)
                {
                    draw.BeginFigure(Project(a), false);
                    draw.LineTo(Project(b));
                    draw.EndFigure(false);
                }
            ctx.DrawGeometry(null, new Pen(new SolidColorBrush(Ux.TextMeta, 0.28), 0.7), skin);
        }

        if (bodies) DrawBodies(ctx, _bodies!);

        var bone = new Pen(Ux.MetaBrush, 1.6);
        foreach (var (from, to) in pose.Links)
            ctx.DrawLine(bone, Project(pose.Bones[from].Position), Project(pose.Bones[to].Position));

        foreach (var b in pose.Bones)
        {
            var at = Project(b.Position);
            bool root = b.Parent < 0;
            bool hovered = b.Name == HoveredBone && HoveredBone.Length > 0;
            double radius = hovered ? 4.5 : root ? 3.5 : 2;
            var brush = hovered ? Ux.AccentBrush : root ? Ux.CodeBrush : Ux.TitleBrush;
            ctx.DrawEllipse(brush, null, at, radius, radius);
        }

        if (HoveredBone.Length > 0)
        {
            var label = new FormattedText(HoveredBone, CultureInfo.InvariantCulture,
                                          FlowDirection.LeftToRight, Typeface.Default, 12, Ux.TitleBrush);
            ctx.DrawText(label, new Point(14, Bounds.Height - 26));
        }
    }

    private void DrawBodies(DrawingContext ctx, HavokRagdollModel model)
    {
        var hull = new Pen(new SolidColorBrush(Ux.Accent, 0.9), 1.2);
        var centre = new Pen(new SolidColorBrush(Ux.Accent, 0.55), 1);

        foreach (var body in model.Bodies)
        {
            var shape = model.Shapes.FirstOrDefault(s => s.Id == body.ShapeId);
            if (shape == null) continue;

            foreach (var ring in shape.FaceRings)
            {
                if (ring.Length < 2) continue;
                for (int i = 0; i < ring.Length; i++)
                {
                    int from = ring[i];
                    int to = ring[(i + 1) % ring.Length];
                    if (from < 0 || from >= shape.Vertices.Count ||
                        to < 0 || to >= shape.Vertices.Count) continue;

                    var a = body.Position + Vector3.Transform(shape.Vertices[from], body.Rotation);
                    var b = body.Position + Vector3.Transform(shape.Vertices[to], body.Rotation);
                    ctx.DrawLine(hull, Project(a), Project(b));
                }
            }

            ctx.DrawEllipse(null, centre, Project(body.Position), 2.2, 2.2);
        }
    }

    private void DrawGround(DrawingContext ctx)
    {
        var pen = new Pen(new SolidColorBrush(Ux.Border, 0.7), 1);
        float reach = (float)(_fit * 1.5);

        for (int i = -2; i <= 2; i++)
        {
            float at = reach * i / 2f;
            ctx.DrawLine(pen, Project(new Vector3(-reach, at, 0) + _centre with { Z = 0 }),
                              Project(new Vector3(reach, at, 0) + _centre with { Z = 0 }));
            ctx.DrawLine(pen, Project(new Vector3(at, -reach, 0) + _centre with { Z = 0 }),
                              Project(new Vector3(at, reach, 0) + _centre with { Z = 0 }));
        }
    }

    protected override void OnPointerPressed(PointerPressedEventArgs e)
    {
        _lastPointer = e.GetPosition(this);
        var props = e.GetCurrentPoint(this).Properties;
        _panning = props.IsMiddleButtonPressed || props.IsRightButtonPressed;
        _orbiting = props.IsLeftButtonPressed;
        e.Pointer.Capture(this);
    }

    protected override void OnPointerMoved(PointerEventArgs e)
    {
        var at = e.GetPosition(this);
        var delta = at - _lastPointer;
        _lastPointer = at;

        if (_orbiting)
        {
            _yaw += delta.X * 0.01;

            _pitch = Math.Clamp(_pitch + delta.Y * 0.01, -1.5, 1.5);
            InvalidateVisual();
            return;
        }

        if (_panning)
        {
            _pan = new Point(_pan.X + delta.X, _pan.Y + delta.Y);
            InvalidateVisual();
            return;
        }

        NameBoneUnder(at);
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e)
    {
        _orbiting = _panning = false;
        e.Pointer.Capture(null);
    }

    protected override void OnPointerWheelChanged(PointerWheelEventArgs e)
    {
        _zoom = Math.Clamp(_zoom * (e.Delta.Y > 0 ? 1.12 : 1 / 1.12), 0.05, 40);
        InvalidateVisual();
        e.Handled = true;
    }

    private void NameBoneUnder(Point at)
    {
        string found = "";
        double nearest = 12 * 12;

        if (_pose != null)
            foreach (var b in _pose.Bones)
            {
                var p = Project(b.Position);
                double d = (p.X - at.X) * (p.X - at.X) + (p.Y - at.Y) * (p.Y - at.Y);
                if (d >= nearest) continue;
                nearest = d;
                found = b.Name;
            }

        if (found == HoveredBone) return;
        HoveredBone = found;
        BoneHovered?.Invoke(found);
        InvalidateVisual();
    }
}
