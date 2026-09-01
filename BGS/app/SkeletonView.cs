using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Numerics;
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

    // The physics skeleton's world frames posed from the animation currently shown, via the
    // file's own hkaSkeletonMapper (or null when the file has no mapper or nothing plays);
    // all physics overlays draw through these when they exist, and otherwise fall back to
    // the measured static file-space reference.
    private HavokBoneFrame[]? _physicsPose;

    public string HoveredBone { get; private set; } = "";
    public Action<string>? BoneHovered;

    public bool ShowReference { get; set; }
    public bool ShowBodies { get; set; }
    public bool ShowConstraints { get; set; }
    public bool ShowBindings { get; set; }
    public bool ShowPhysicsSkeleton { get; set; }
    public bool ShowFrames { get; set; }

    public int DrawnBones => _pose?.Bones.Count ?? 0;
    public int DrawnEdges => _mesh?.Length ?? 0;
    public int DrawnBodies => _bodies?.Bodies.Count ?? 0;
    public int DrawnConstraints => _bodies?.Constraints.Count ?? 0;
    public int DrawnBindings => _bodies?.BoneBindings.Count ?? 0;
    public int DrawnPhysicsBones => _bodies?.Skeleton?.ReferencePose.Count ?? 0;
    public int DrawnFrames => _bodies?.Bodies.Count ?? 0;
    public int DrawnMappings => _bodies?.Mappings.Count ?? 0;

    // The fitted half-extent of the rigid bodies (FitBodies' bounding-box half-size) and the
    // length of the axis stubs the Body frames overlay draws, in the file's own units. Both
    // are published for the Playback viewport status line, so ragdoll dimensions can be
    // sanity-checked across files.
    public float BodyFit => _bodyFit;
    public float AxisStubLength => Math.Max(0.5f, _bodyFit * 0.06f);

    // The scale/unit readout for the Playback viewport status line. Empty when no ragdoll
    // model is loaded, so the status pill stays hidden.
    public string ScaleStatusText
    {
        get
        {
            if (_bodies == null || _bodies.Bodies.Count == 0) return "";
            return "body fit " + _bodyFit.ToString("0.00", CultureInfo.InvariantCulture) + ", " +
                   "frame stub " + AxisStubLength.ToString("0.00", CultureInfo.InvariantCulture) +
                   " file units";
        }
    }

    private int _hoveredBody = -1;

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
        UpdatePhysicsPose();
        InvalidateVisual();
    }

    public void Update(AnimationPose.Pose? pose)
    {
        _pose = pose;
        UpdatePhysicsPose();
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
        ShowConstraints = false;
        ShowBindings = false;
        ShowPhysicsSkeleton = false;
        ShowFrames = false;
        _hoveredBody = -1;
        _physicsPose = null;
        _yaw = 0.6;
        _pitch = 0.25;
        _zoom = 1;
        _pan = default;
        InvalidateVisual();
    }

    public void SetBodies(HavokRagdollModel? model)
    {
        _bodies = model;
        UpdatePhysicsPose();
        FitBodies();
        InvalidateVisual();
    }

    // Re-compose the physics skeleton from the animation pose currently shown. When the file
    // carries a mapper and a pose is present, every physics bone gets a mapped world frame;
    // otherwise the overlays keep using the measured static reference.
    private void UpdatePhysicsPose()
    {
        if (_bodies == null || _pose == null || _bodies.Mappings.Count == 0)
        {
            _physicsPose = null;
            return;
        }

        var source = new HavokBoneFrame[_pose.Bones.Count];
        for (int i = 0; i < _pose.Bones.Count; i++)
            source[i] = new HavokBoneFrame(_pose.Bones[i].Position, _pose.Bones[i].Rotation);

        _physicsPose = _bodies.MapPose(source);
    }

    // The world frame a body draws at: its measured cinfo frame, or the pose of the physics
    // bone it is bound to when the ragdoll follows the animation. A body with no binding to
    // a posed bone keeps its measured frame.
    private (Vector3 Position, Quaternion Rotation) BodyFrame(HavokRagdollModel model, HavokRigidBody body)
    {
        if (_physicsPose != null)
            foreach (var binding in model.BoneBindings)
                if (binding.BodyId == body.Id &&
                    binding.BoneIndex >= 0 && binding.BoneIndex < _physicsPose.Length)
                    return (_physicsPose[binding.BoneIndex].Position, _physicsPose[binding.BoneIndex].Rotation);
        return (body.Position, body.Rotation);
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
        bool physicsSkeleton = ShowPhysicsSkeleton &&
                               _bodies is { Skeleton: { ParentIndices.Count: > 0, ReferencePose.Count: > 0 } };
        if ((pose == null || pose.Bones.Count == 0) && !bodies && !physicsSkeleton)
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
        if (ShowConstraints && _bodies != null && _bodies.Constraints.Count > 0)
            DrawConstraints(ctx, _bodies);

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

        if (ShowBindings && _bodies is { BoneBindings.Count: > 0, Skeleton: { BoneNames.Count: > 0 } })
            DrawBindings(ctx, _bodies);

        if (physicsSkeleton) DrawPhysicsSkeleton(ctx, _bodies!);

        if (ShowFrames && _bodies is { Bodies.Count: > 0 })
            DrawFrames(ctx, _bodies);

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
            var (position, rotation) = BodyFrame(model, body);
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

                    var a = position + Vector3.Transform(shape.Vertices[from], rotation);
                    var b = position + Vector3.Transform(shape.Vertices[to], rotation);
                    ctx.DrawLine(hull, Project(a), Project(b));
                }
            }

            ctx.DrawEllipse(null, centre, Project(position), 2.2, 2.2);
        }
    }

    // The physics skeleton hknpRagdollData carries, drawn as its own bone chain: the
    // reference pose composed down the parent indices into file space, in green so it reads
    // apart from the animation skeleton (gray), the bodies (blue) and the constraints
    // (amber). Root bones get a slightly larger marker.
    private void DrawPhysicsSkeleton(DrawingContext ctx, HavokRagdollModel model)
    {
        var chain = new Pen(new SolidColorBrush(Ux.Good, 0.95), 2);
        var marker = new SolidColorBrush(Ux.Good, 0.9);

        // Posed by the current animation when a mapper is present; otherwise the measured
        // reference pose composed down the parent chain.
        Vector3[] positions = _physicsPose != null
            ? _physicsPose.Take(model.Skeleton!.BoneNames.Count).Select(p => p.Position).ToArray()
            : model.Skeleton!.WorldReferencePositions().ToArray();
        var parents = model.Skeleton.ParentIndices;

        for (int i = 0; i < positions.Length && i < parents.Count; i++)
        {
            int parent = parents[i];
            if (parent >= 0 && parent < positions.Length)
                ctx.DrawLine(chain, Project(positions[parent]), Project(positions[i]));
        }

        for (int i = 0; i < positions.Length; i++)
        {
            bool root = i >= parents.Count || parents[i] < 0;
            double radius = root ? 3.2 : 2.2;
            ctx.DrawEllipse(marker, null, Project(positions[i]), radius, radius);
        }
    }

    // Each body's measured local frame (body cinfo position + orientation) drawn as a
    // conventional red/green/blue axis triad, plus a marker at the measured world-space
    // center of mass (motionCinfos centerOfMassWorld). The triad length scales with the
    // model so it stays readable across files.
    private void DrawFrames(DrawingContext ctx, HavokRagdollModel model)
    {
        var xAxis = new Pen(new SolidColorBrush(Ux.Bad, 0.95), 1.6);
        var yAxis = new Pen(new SolidColorBrush(Ux.Good, 0.95), 1.6);
        var zAxis = new Pen(new SolidColorBrush(Ux.Accent, 0.95), 1.6);
        var comPen = new Pen(new SolidColorBrush(Ux.TextTitle, 0.9), 1.2);

        // The stub length is published as AxisStubLength for the viewport status readout.
        float arm = AxisStubLength;

        foreach (var body in model.Bodies)
        {
            var (position, rotation) = BodyFrame(model, body);
            var at = Project(position);
            ctx.DrawLine(xAxis, at, Project(position + Vector3.Transform(Vector3.UnitX, rotation) * arm));
            ctx.DrawLine(yAxis, at, Project(position + Vector3.Transform(Vector3.UnitY, rotation) * arm));
            ctx.DrawLine(zAxis, at, Project(position + Vector3.Transform(Vector3.UnitZ, rotation) * arm));

            // Zero is the sentinel for "no measured COM" (see HavokRigidBody.CenterOfMass).
            // The COM is measured in file space, so when the body moves it keeps its
            // body-relative offset from the measured frame.
            if (body.CenterOfMass == Vector3.Zero) continue;
            var com = position + Vector3.Transform(body.CenterOfMass - body.Position, rotation);
            ctx.DrawEllipse(null, comPen, Project(com), 2.6, 2.6);
        }
    }

    private void DrawBindings(DrawingContext ctx, HavokRagdollModel model)
    {
        var labelPen = new Pen(new SolidColorBrush(Ux.Accent, 0.75), 1);
        var hoveredBody = model.Bodies.FirstOrDefault(b => b.Id == _hoveredBody);

        foreach (var binding in model.BoneBindings)
        {
            var body = model.Bodies.FirstOrDefault(b => b.Id == binding.BodyId);
            if (body == null || model.Skeleton == null ||
                binding.BoneIndex < 0 || binding.BoneIndex >= model.Skeleton.BoneNames.Count)
                continue;

            string label = model.Skeleton.BoneNames[binding.BoneIndex];
            if (label.Length == 0) continue;

            bool hovered = binding.BodyId == _hoveredBody;
            var (position, rotation) = BodyFrame(model, body);
            var at = Project(position);
            var text = new FormattedText(label, CultureInfo.InvariantCulture,
                                         FlowDirection.LeftToRight, Typeface.Default,
                                         hovered ? 12 : 11,
                                         hovered ? Ux.AccentBrush : Ux.MetaBrush);
            ctx.DrawText(text, new Point(at.X + 7, at.Y - 15));
        }

        // The hovered body itself is highlighted by re-drawing its hull in accent so the
        // body-to-bone association reads at a glance; the label above already changed colour.
        if (hoveredBody == null || model.Skeleton == null) return;
        var shape = model.Shapes.FirstOrDefault(s => s.Id == hoveredBody.ShapeId);
        if (shape == null) return;

        foreach (var ring in shape.FaceRings)
        {
            if (ring.Length < 2) continue;
            for (int i = 0; i < ring.Length; i++)
            {
                int from = ring[i];
                int to = ring[(i + 1) % ring.Length];
                if (from < 0 || from >= shape.Vertices.Count ||
                    to < 0 || to >= shape.Vertices.Count) continue;

                var (hoveredPosition, hoveredRotation) = BodyFrame(model, hoveredBody);
                var a = hoveredPosition + Vector3.Transform(shape.Vertices[from], hoveredRotation);
                var b = hoveredPosition + Vector3.Transform(shape.Vertices[to], hoveredRotation);
                ctx.DrawLine(labelPen, Project(a), Project(b));
            }
        }
    }

    private const float ConstraintArm = 8f;

    private void DrawConstraints(DrawingContext ctx, HavokRagdollModel model)
    {
        var link = new Pen(new SolidColorBrush(Ux.Warn, 0.55), 1);
        var pivot = new Pen(new SolidColorBrush(Ux.Warn, 0.95), 1.2);
        var arm = new Pen(new SolidColorBrush(Ux.Warn, 0.85), 1.1);

        foreach (var constraint in model.Constraints)
        {
            var bodyA = model.Bodies.FirstOrDefault(b => b.Id == constraint.BodyA);
            var bodyB = model.Bodies.FirstOrDefault(b => b.Id == constraint.BodyB);
            if (bodyA == null || bodyB == null) continue;

            var (frameA, rotationA) = BodyFrame(model, bodyA);
            var (frameB, rotationB) = BodyFrame(model, bodyB);

            ctx.DrawLine(link, Project(frameA), Project(frameB));

            if (constraint.FrameA == null) continue;
            var pivotA = frameA + Vector3.Transform(Translation(constraint.FrameA), rotationA);
            ctx.DrawEllipse(null, pivot, Project(pivotA), 2.2, 2.2);

            Vector3 pivotB = pivotA;
            if (constraint.FrameB != null)
            {
                pivotB = frameB + Vector3.Transform(Translation(constraint.FrameB), rotationB);
                ctx.DrawEllipse(null, pivot, Project(pivotB), 2.2, 2.2);
            }

            switch (constraint.Kind)
            {
                case HavokConstraintKind.Ragdoll:
                    DrawTwist(ctx, arm, constraint, rotationA, pivotA);
                    DrawCone(ctx, arm, constraint, rotationA, pivotA);
                    break;
                case HavokConstraintKind.LimitedHinge:
                    DrawHinge(ctx, arm, constraint, rotationA, pivotB, pivotA);
                    break;
            }
        }
    }

    private void DrawTwist(DrawingContext ctx, Pen pen, HavokConstraint constraint,
                                  Quaternion rotationA, Vector3 pivotA)
    {
        if (constraint.FrameA == null) return;
        var axis = Vector3.Normalize(WorldDir(constraint.FrameA, constraint.TwistAxis, rotationA));
        var reference = Vector3.Normalize(WorldDir(constraint.FrameA, constraint.TwistRefAxis, rotationA));

        ctx.DrawLine(pen, Project(pivotA), Project(pivotA + axis * ConstraintArm));

        if (constraint.TwistMinAngle is float min && constraint.TwistMaxAngle is float max)
        {
            ctx.DrawLine(pen, Project(pivotA),
                              Project(pivotA + RotateAround(reference, axis, min) * ConstraintArm));
            ctx.DrawLine(pen, Project(pivotA),
                              Project(pivotA + RotateAround(reference, axis, max) * ConstraintArm));
        }
    }

    private void DrawCone(DrawingContext ctx, Pen pen, HavokConstraint constraint,
                                 Quaternion rotationA, Vector3 pivotA)
    {
        if (constraint.FrameA == null || constraint.ConeMaxAngle is not float coneMax) return;
        var axis = Vector3.Normalize(WorldDir(constraint.FrameA, constraint.ConeTwistAxis, rotationA));

        ctx.DrawLine(pen, Project(pivotA), Project(pivotA + axis * ConstraintArm));

        var centre = pivotA + axis * (MathF.Cos(coneMax) * ConstraintArm);
        var radius = MathF.Sin(coneMax) * ConstraintArm;
        var right = Vector3.Normalize(Vector3.Cross(axis, Vector3.UnitY));
        if (right.LengthSquared() < 1e-6f) right = Vector3.Normalize(Vector3.Cross(axis, Vector3.UnitX));
        var up = Vector3.Normalize(Vector3.Cross(axis, right));

        const int segments = 24;
        var previous = Project(centre + right * radius);
        for (int i = 1; i <= segments; i++)
        {
            float angle = MathF.PI * 2 * i / segments;
            var point = centre + (right * MathF.Cos(angle) + up * MathF.Sin(angle)) * radius;
            var at = Project(point);
            ctx.DrawLine(pen, previous, at);
            previous = at;
        }
    }

    private void DrawHinge(DrawingContext ctx, Pen pen, HavokConstraint constraint,
                                  Quaternion rotationA, Vector3 pivotB, Vector3 pivotA)
    {
        if (constraint.FrameA == null) return;
        var axis = Vector3.Normalize(WorldDir(constraint.FrameA, constraint.LimitAxis, rotationA));

        ctx.DrawLine(pen, Project(pivotA), Project(pivotA + axis * ConstraintArm));

        if (constraint.MinAngle == 0 && constraint.MaxAngle == 0) return;

        // The file stores no explicit angle reference for the hinge limit, so the wedge uses
        // the measured A->B direction (projected perpendicular to the limit axis) as its zero.
        var toward = pivotB - pivotA;
        var perpendicular = toward - axis * Vector3.Dot(toward, axis);
        var reference = perpendicular.LengthSquared() < 1e-8f
            ? Vector3.Normalize(Vector3.Cross(axis, Vector3.UnitY))
            : Vector3.Normalize(perpendicular);

        ctx.DrawLine(pen, Project(pivotA),
                          Project(pivotA + RotateAround(reference, axis, constraint.MinAngle) * ConstraintArm));
        ctx.DrawLine(pen, Project(pivotA),
                          Project(pivotA + RotateAround(reference, axis, constraint.MaxAngle) * ConstraintArm));
    }

    private static Vector3 Translation(float[] frame) => new(frame[12], frame[13], frame[14]);

    private static Vector3 Column(float[] frame, int axis) =>
        new(frame[4 * axis], frame[4 * axis + 1], frame[4 * axis + 2]);

    private static Vector3 WorldDir(float[] frame, int axis, Quaternion rotation) =>
        Vector3.Transform(Column(frame, axis), rotation);

    private static Vector3 RotateAround(Vector3 vector, Vector3 axis, float angle) =>
        Vector3.Transform(vector, Quaternion.CreateFromAxisAngle(axis, angle));

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
        NameBodyUnder(at);
    }

    private void NameBodyUnder(Point at)
    {
        if (!ShowBindings || _bodies == null || _bodies.Bodies.Count == 0) return;

        int found = -1;
        double nearest = 12 * 12;

        foreach (var body in _bodies.Bodies)
        {
            var p = Project(BodyFrame(_bodies, body).Position);
            double d = (p.X - at.X) * (p.X - at.X) + (p.Y - at.Y) * (p.Y - at.Y);
            if (d >= nearest) continue;
            nearest = d;
            found = body.Id;
        }

        if (found == _hoveredBody) return;
        _hoveredBody = found;
        InvalidateVisual();
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
