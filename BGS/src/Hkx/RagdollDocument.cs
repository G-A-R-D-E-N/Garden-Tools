using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;

namespace OpenCommonwealth.Services.Hkx;
public sealed class RagdollDocument
{
    private byte[] _source;
    private DocumentSourceStamp _stamp;

    private readonly List<RagdollNode> _ragdolls = new();
    private readonly List<RagdollNode> _savedRagdolls = new();
    private readonly List<RagdollShape> _shapes = new();
    private readonly List<RagdollShape> _savedShapes = new();

    private RagdollDocument(byte[] source, string? path)
    {
        ArgumentNullException.ThrowIfNull(source);

        Path = path;
        _source = (byte[])source.Clone();
        _stamp = DocumentSourceStamp.Capture(_source);
        Reload();
    }

    public RagdollDocument(byte[] source) : this(source, null)
    {
    }

    public static RagdollDocument Open(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        string full = System.IO.Path.GetFullPath(path);
        return new RagdollDocument(InputFilePolicy.ReadHkx(full), full);
    }

    public string? Path { get; }

    public bool Dirty => RagdollChanges().Count > 0 || ShapeChanges().Count > 0;

    public IReadOnlyList<RagdollNode> Ragdolls => _ragdolls;

    public IReadOnlyList<RagdollShape> Shapes => _shapes;

    public void SetBodyMass(int ragdoll, int body, float mass)
    {
        var node = Node(ragdoll);
        var row = Body(node, body);
        var motion = Motion(node, row.MotionId, row.Index);

        if (!float.IsFinite(mass) || mass <= 0f)
            throw new InvalidOperationException(
                $"a mass of {mass} is not a positive finite number, so body {body} was not changed");
        if (!float.IsFinite(motion.InverseMass) || motion.InverseMass <= 0f)
            throw new InvalidOperationException(
                $"body {body} of ragdoll #{node.ObjectId} stores an inverse mass of {motion.InverseMass}, " +
                "so its inverse inertia cannot be scaled by a mass ratio and nothing was changed");

        float inverseMass = 1f / mass;
        float ratio = inverseMass / motion.InverseMass;
        var inertia = motion.InverseInertiaLocal * ratio;
        if (!RagdollValues.Finite(inertia))
            throw new InvalidOperationException(
                $"scaling body {body}'s inverse inertia by {ratio} leaves a non-finite lane, so nothing was changed");

        motion.InverseMass = inverseMass;
        motion.InverseInertiaLocal = inertia;
    }

    public void SetBodyCenterOfMass(int ragdoll, int body, Vector3 world)
    {
        var node = Node(ragdoll);
        var row = Body(node, body);
        if (!RagdollValues.Finite(world))
            throw new InvalidOperationException(
                $"the centre of mass for body {body} of ragdoll #{node.ObjectId} is not finite, so nothing was changed");

        Motion(node, row.MotionId, row.Index).CenterOfMassWorld = world;
    }

    public void SetMaterialFriction(int ragdoll, int material, float friction, bool allowShared = false)
    {
        var entry = Material(Node(ragdoll), material);
        RequireSharedAllowed(entry, allowShared, "friction");

        if (!RagdollValues.TryHalf(friction, out float rounded) || rounded < 0f)
            throw new InvalidOperationException(
                $"a friction of {friction} is not a non-negative number this file can store at half precision, " +
                $"so material {material} was not changed");

        entry.DynamicFriction = rounded;
        entry.StaticFriction = rounded;
    }

    public void SetMaterialRestitution(int ragdoll, int material, float restitution, bool allowShared = false)
    {
        var entry = Material(Node(ragdoll), material);
        RequireSharedAllowed(entry, allowShared, "restitution");

        if (!RagdollValues.TryHalf(restitution, out float rounded) || rounded < 0f)
            throw new InvalidOperationException(
                $"a restitution of {restitution} is not a non-negative number this file can store at half " +
                $"precision, so material {material} was not changed");

        entry.Restitution = rounded;
    }

    public void SetShapeRadius(int shape, float radius)
    {
        var target = Shape(shape);
        if (!float.IsFinite(radius) || radius <= 0f)
            throw new InvalidOperationException(
                $"a radius of {radius} is not a positive finite number, so shape {shape} was not changed");

        target.Radius = radius;
    }

    public void SetCapsuleEndpoints(int shape, Vector3 a, Vector3 b)
    {
        var target = Shape(shape);
        if (!RagdollValues.Finite(a) || !RagdollValues.Finite(b))
            throw new InvalidOperationException(
                $"the endpoints for shape {shape} are not finite, so nothing was changed");

        target.A = a;
        target.B = b;
    }

    public void SetConstraintLimits(int ragdoll, int constraint, float minAngle, float maxAngle)
    {
        var entry = EditableConstraint(Node(ragdoll), constraint);
        RequireOrderedLimits(minAngle, maxAngle, $"{LimitName(entry)} of constraint {constraint}");

        entry.MinAngle = minAngle;
        entry.MaxAngle = maxAngle;
        if (entry.IsRagdoll)
        {
            entry.PlaneMinAngle = minAngle;
            entry.PlaneMaxAngle = maxAngle;
        }
    }

    public void SetConstraintConeLimit(int ragdoll, int constraint, float maxAngle)
    {
        var entry = EditableConstraint(Node(ragdoll), constraint);
        if (!entry.IsRagdoll)
            throw new InvalidOperationException(
                $"constraint {constraint} of ragdoll {ragdoll} is a {entry.ClassName}, which has no cone limit");

        if (!float.IsFinite(maxAngle) || maxAngle < 0f || maxAngle > MathF.PI)
            throw new InvalidOperationException(
                $"a cone limit of {maxAngle} is not within 0 and pi, so constraint {constraint} was not changed");

        entry.ConeMaxAngle = maxAngle;
    }

    public void SetConstraintPlaneLimits(int ragdoll, int constraint, float minAngle, float maxAngle)
    {
        var entry = EditableConstraint(Node(ragdoll), constraint);
        if (!entry.IsRagdoll)
            throw new InvalidOperationException(
                $"constraint {constraint} of ragdoll {ragdoll} is a {entry.ClassName}, which has no plane limit");

        RequireOrderedLimits(minAngle, maxAngle, $"planes limit of constraint {constraint}");
        entry.PlaneMinAngle = minAngle;
        entry.PlaneMaxAngle = maxAngle;
    }

    public void SetConstraintFriction(int ragdoll, int constraint, float friction)
    {
        var entry = EditableConstraint(Node(ragdoll), constraint);
        if (!float.IsFinite(friction) || friction < 0f)
            throw new InvalidOperationException(
                $"a friction of {friction} is not a non-negative finite number, so constraint {constraint} " +
                "was not changed");

        entry.Friction = friction;
    }

    public byte[] Serialize()
    {
        var changed = RagdollChanges();
        var shapeChanged = ShapeChanges();
        if (changed.Count == 0 && shapeChanged.Count == 0) return (byte[])_source.Clone();

        return RagdollWriteback.Write(_source, _ragdolls, _shapes, changed, shapeChanged);
    }

    internal byte[] ForceRebuild() =>
        RagdollWriteback.Write(_source, _ragdolls, _shapes, Array.Empty<RagdollChange>(),
                               Array.Empty<RagdollShapeChange>(), force: true);

    public void Save()
    {
        string path = Path
            ?? throw new InvalidOperationException("this document was not opened from a file, so it has nowhere to save");
        if (!Dirty) return;

        byte[] bytes = Serialize();
        FileSafety.ReplaceChecked(path, bytes, _stamp);
        _source = bytes;
        _stamp = DocumentSourceStamp.Capture(_source);
        Reload();
    }

    public void Reload()
    {
        _ragdolls.Clear();
        _savedRagdolls.Clear();
        _shapes.Clear();
        _savedShapes.Clear();

        var image = PackfileImage.Read(_source);
        NativeLayout.RequireWritable(image);

        var objects = new PackfileObjects(image);
        foreach (var instance in objects.Instances)
        {
            if (instance.ClassName == RagdollLayout.RagdollClass)
            {
                if (RagdollRead.Read(objects, instance) is not { } node)
                    throw new InvalidDataException(
                        $"ragdoll {instance.Offset:x} carries a member this build cannot place, so nothing was " +
                        "loaded: a write-back would silently drop it");
                _ragdolls.Add(node);
                continue;
            }

            if (instance.ClassName != RagdollLayout.CapsuleClass) continue;
            if (RagdollRead.ReadShape(objects, instance) is { } shape) _shapes.Add(shape);
        }

        if (_ragdolls.Count == 0)
            throw new InvalidDataException(
                "This file holds no hknpRagdollData this build can read, so there is nothing to edit.");

        foreach (var node in _ragdolls) _savedRagdolls.Add(node.Copy());
        foreach (var shape in _shapes) _savedShapes.Add(shape.Copy());
    }

    internal List<RagdollChange> RagdollChanges()
    {
        var changed = new List<RagdollChange>();
        for (int i = 0; i < _ragdolls.Count; i++)
            if (!_ragdolls[i].SameAs(_savedRagdolls[i]))
                changed.Add(new RagdollChange(_ragdolls[i], _savedRagdolls[i]));
        return changed;
    }

    internal List<RagdollShapeChange> ShapeChanges()
    {
        var changed = new List<RagdollShapeChange>();
        for (int i = 0; i < _shapes.Count; i++)
            if (!_shapes[i].SameAs(_savedShapes[i]))
                changed.Add(new RagdollShapeChange(_shapes[i], _savedShapes[i]));
        return changed;
    }

    internal static string LimitName(RagdollConstraint constraint) =>
        constraint.IsRagdoll ? "twist limit" : "angular limit";

    private static void RequireOrderedLimits(float min, float max, string what)
    {
        if (!float.IsFinite(min) || !float.IsFinite(max) || min > max ||
            min < -MathF.PI || max > MathF.PI)
            throw new InvalidOperationException(
                $"the {what} must be finite and ordered within -pi and pi, and {min} to {max} is not");
    }

    private static void RequireSharedAllowed(RagdollMaterial material, bool allowShared, string what)
    {
        if (allowShared || !material.Shared) return;

        throw new InvalidOperationException(
            $"material {material.Index} is shared by {material.ReferencedByBodies} body(ies), so its {what} " +
            "was not changed; pass allowShared to change it for every body");
    }

    private RagdollNode Node(int ragdoll)
    {
        if (ragdoll < 0 || ragdoll >= _ragdolls.Count)
            throw new InvalidOperationException(
                $"this file holds {_ragdolls.Count} ragdoll(s), so there is no ragdoll {ragdoll}");
        return _ragdolls[ragdoll];
    }

    private static RagdollBody Body(RagdollNode node, int body)
    {
        if (body < 0 || body >= node.Bodies.Count)
            throw new InvalidOperationException(
                $"ragdoll #{node.ObjectId} holds {node.Bodies.Count} body(ies), so there is no body {body}");
        return node.Bodies[body];
    }

    private static RagdollMotion Motion(RagdollNode node, int motionId, int body)
    {
        if (motionId < 0 || motionId >= node.Motions.Count)
            throw new InvalidOperationException(
                $"body {body} of ragdoll #{node.ObjectId} names motion {motionId}, and the ragdoll holds " +
                $"{node.Motions.Count} motion(s), so nothing was changed");
        return node.Motions[motionId];
    }

    private static RagdollMaterial Material(RagdollNode node, int material)
    {
        if (material < 0 || material >= node.Materials.Count)
            throw new InvalidOperationException(
                $"ragdoll #{node.ObjectId} holds {node.Materials.Count} material(s), so there is no material {material}");
        return node.Materials[material];
    }

    private static RagdollConstraint EditableConstraint(RagdollNode node, int constraint)
    {
        if (constraint < 0 || constraint >= node.Constraints.Count)
            throw new InvalidOperationException(
                $"ragdoll #{node.ObjectId} holds {node.Constraints.Count} constraint(s), so there is no " +
                $"constraint {constraint}");

        var entry = node.Constraints[constraint];
        if (!entry.LimitsEditable)
            throw new InvalidOperationException(
                $"constraint {constraint} of ragdoll #{node.ObjectId} is a " +
                $"{(entry.ClassName.Length > 0 ? entry.ClassName : "constraint with no data object")}, " +
                "whose limits this build does not write");
        return entry;
    }

    private RagdollShape Shape(int shape)
    {
        if (shape < 0 || shape >= _shapes.Count)
            throw new InvalidOperationException(
                $"this file holds {_shapes.Count} capsule shape(s), so there is no shape {shape}");
        return _shapes[shape];
    }
}
