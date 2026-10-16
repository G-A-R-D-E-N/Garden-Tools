using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;

namespace OpenCommonwealth.Services.Hkx;

public sealed class RagdollBody
{
    internal RagdollBody(int index, int motionId, int materialId, int shapeObjectId, string name)
    {
        Index = index;
        MotionId = motionId;
        MaterialId = materialId;
        ShapeObjectId = shapeObjectId;
        Name = name;
    }

    public int Index { get; }

    public int MotionId { get; }

    public int MaterialId { get; }

    public int ShapeObjectId { get; }

    public string Name { get; }
}

public sealed class RagdollMotion
{
    internal RagdollMotion(int index)
    {
        Index = index;
    }

    public int Index { get; }

    public float InverseMass { get; internal set; }

    public Vector3 InverseInertiaLocal { get; internal set; }

    public Vector3 CenterOfMassWorld { get; internal set; }

    public float Mass => InverseMass > 0f && float.IsFinite(InverseMass) ? 1f / InverseMass : 0f;

    internal RagdollMotion Copy()
    {
        var copy = new RagdollMotion(Index)
        {
            InverseMass = InverseMass,
            InverseInertiaLocal = InverseInertiaLocal,
            CenterOfMassWorld = CenterOfMassWorld,
        };
        return copy;
    }
}

public sealed class RagdollMaterial
{
    internal RagdollMaterial(int index)
    {
        Index = index;
    }

    public int Index { get; }

    public float DynamicFriction { get; internal set; }

    public float StaticFriction { get; internal set; }

    public float Restitution { get; internal set; }

    public bool FileSaysShared { get; internal set; }

    public int ReferencedByBodies { get; internal set; }

    public bool Shared => FileSaysShared || ReferencedByBodies > 1;

    internal RagdollMaterial Copy()
    {
        var copy = new RagdollMaterial(Index)
        {
            DynamicFriction = DynamicFriction,
            StaticFriction = StaticFriction,
            Restitution = Restitution,
            FileSaysShared = FileSaysShared,
            ReferencedByBodies = ReferencedByBodies,
        };
        return copy;
    }
}

public sealed class RagdollShape
{
    internal RagdollShape(int instanceIndex, int objectId, string className)
    {
        InstanceIndex = instanceIndex;
        ObjectId = objectId;
        ClassName = className;
    }

    public int InstanceIndex { get; }

    public int ObjectId { get; }

    public string ClassName { get; }

    public Vector3 A { get; internal set; }

    public Vector3 B { get; internal set; }

    public float Radius { get; internal set; }

    internal RagdollShape Copy()
    {
        var copy = new RagdollShape(InstanceIndex, ObjectId, ClassName)
        {
            A = A,
            B = B,
            Radius = Radius,
        };
        return copy;
    }

    internal bool SameAs(RagdollShape other) =>
        InstanceIndex == other.InstanceIndex && ObjectId == other.ObjectId && Radius == other.Radius &&
        A == other.A && B == other.B;
}

public sealed class RagdollConstraint
{
    internal RagdollConstraint(int index, int objectId, int instanceIndex, string className)
    {
        Index = index;
        ObjectId = objectId;
        InstanceIndex = instanceIndex;
        ClassName = className;
    }

    public int Index { get; }

    public int ObjectId { get; }

    public int InstanceIndex { get; }

    public string ClassName { get; }

    public bool LimitsEditable =>
        InstanceIndex >= 0 &&
        ClassName is RagdollLayout.RagdollConstraintClass or RagdollLayout.LimitedHingeConstraintClass;

    public bool IsRagdoll => ClassName == RagdollLayout.RagdollConstraintClass;

    public float Friction { get; internal set; }

    public float MinAngle { get; internal set; }

    public float MaxAngle { get; internal set; }

    public float ConeMaxAngle { get; internal set; }

    public float PlaneMinAngle { get; internal set; }

    public float PlaneMaxAngle { get; internal set; }

    internal RagdollConstraint Copy()
    {
        var copy = new RagdollConstraint(Index, ObjectId, InstanceIndex, ClassName)
        {
            Friction = Friction,
            MinAngle = MinAngle,
            MaxAngle = MaxAngle,
            ConeMaxAngle = ConeMaxAngle,
            PlaneMinAngle = PlaneMinAngle,
            PlaneMaxAngle = PlaneMaxAngle,
        };
        return copy;
    }

    internal bool SameAs(RagdollConstraint other) =>
        Index == other.Index && ObjectId == other.ObjectId && InstanceIndex == other.InstanceIndex &&
        ClassName == other.ClassName && Friction == other.Friction && MinAngle == other.MinAngle &&
        MaxAngle == other.MaxAngle && ConeMaxAngle == other.ConeMaxAngle &&
        PlaneMinAngle == other.PlaneMinAngle && PlaneMaxAngle == other.PlaneMaxAngle;
}

public sealed class RagdollNode
{
    internal RagdollNode(PackfileObjects.Instance instance, int objectId)
    {
        Instance = instance;
        ObjectId = objectId;
    }

    internal PackfileObjects.Instance Instance { get; }

    public int ObjectId { get; }

    public HavokRagdollSkeleton? Skeleton { get; internal set; }

    public IReadOnlyList<int> BoneToBodyMap { get; internal set; } = Array.Empty<int>();

    public IReadOnlyList<RagdollBody> Bodies { get; internal set; } = Array.Empty<RagdollBody>();

    public IReadOnlyList<RagdollMotion> Motions { get; internal set; } = Array.Empty<RagdollMotion>();

    public IReadOnlyList<RagdollMaterial> Materials { get; internal set; } = Array.Empty<RagdollMaterial>();

    public IReadOnlyList<RagdollConstraint> Constraints { get; internal set; } = Array.Empty<RagdollConstraint>();

    internal RagdollNode Copy()
    {
        var copy = new RagdollNode(Instance, ObjectId)
        {
            Skeleton = Skeleton,
            BoneToBodyMap = BoneToBodyMap,
            Bodies = Bodies.ToList(),
            Motions = Motions.Select(motion => motion.Copy()).ToList(),
            Materials = Materials.Select(material => material.Copy()).ToList(),
            Constraints = Constraints.Select(constraint => constraint.Copy()).ToList(),
        };
        return copy;
    }

    internal bool SameAs(RagdollNode other) =>
        ObjectId == other.ObjectId &&
        Bodies.Count == other.Bodies.Count && Motions.Count == other.Motions.Count &&
        Materials.Count == other.Materials.Count && Constraints.Count == other.Constraints.Count &&
        Bodies.Select(body => (body.Index, body.MotionId, body.MaterialId, body.ShapeObjectId))
              .SequenceEqual(other.Bodies.Select(body => (body.Index, body.MotionId, body.MaterialId,
                                                          body.ShapeObjectId))) &&
        Motions.Select(motion => (motion.Index, motion.InverseMass, motion.InverseInertiaLocal,
                                  motion.CenterOfMassWorld))
               .SequenceEqual(other.Motions.Select(motion => (motion.Index, motion.InverseMass,
                                                              motion.InverseInertiaLocal,
                                                              motion.CenterOfMassWorld))) &&
        Materials.Select(material => (material.Index, material.DynamicFriction, material.StaticFriction,
                                      material.Restitution, material.FileSaysShared))
                 .SequenceEqual(other.Materials.Select(material => (material.Index, material.DynamicFriction,
                                                                    material.StaticFriction, material.Restitution,
                                                                    material.FileSaysShared))) &&
        Constraints.Select(constraint => (constraint.Index, constraint.InstanceIndex, constraint.ClassName,
                                          constraint.Friction, constraint.MinAngle, constraint.MaxAngle,
                                          constraint.ConeMaxAngle, constraint.PlaneMinAngle,
                                          constraint.PlaneMaxAngle))
                   .SequenceEqual(other.Constraints.Select(constraint =>
                       (constraint.Index, constraint.InstanceIndex, constraint.ClassName, constraint.Friction,
                        constraint.MinAngle, constraint.MaxAngle, constraint.ConeMaxAngle,
                        constraint.PlaneMinAngle, constraint.PlaneMaxAngle)));
}

internal static class RagdollLayout
{
    internal const string RagdollClass = "hknpRagdollData";
    internal const string PhysicsSystemClass = "hknpPhysicsSystemData";
    internal const string BodyClass = "hknpBodyCinfo";
    internal const string MotionClass = "hknpMotionCinfo";
    internal const string MaterialClass = "hknpMaterial";
    internal const string MotionPropertiesClass = "hknpMotionProperties";
    internal const string ConstraintCinfoClass = "hknpConstraintCinfo";
    internal const string ShapeClass = "hknpShape";
    internal const string CapsuleClass = "hknpCapsuleShape";
    internal const string ConstraintDataClass = "hkpConstraintData";
    internal const string RagdollConstraintClass = "hkpRagdollConstraintData";
    internal const string LimitedHingeConstraintClass = "hkpLimitedHingeConstraintData";
    internal const string RagdollAtomsClass = "hkpRagdollConstraintDataAtoms";
    internal const string LimitedHingeAtomsClass = "hkpLimitedHingeConstraintDataAtoms";

    internal static readonly string[] SystemArrays =
    {
        "materials", "motionProperties", "motionCinfos", "bodyCinfos", "constraintCinfos",
    };

    internal static string[] ElementClasses =>
        new[] { MaterialClass, MotionPropertiesClass, MotionClass, BodyClass, ConstraintCinfoClass };

    internal static int Field(string className, string member) =>
        HavokClasses.Shipped.Field(className, member)?.Offset
        ?? throw new InvalidDataException($"the class table has no {className}.{member}");

    internal static int Stride(string className)
    {
        int? size = HavokClassTypes.Shipped[className]?.Size;
        if (size is not int value || value <= 0)
            throw new InvalidDataException($"the class table has no size for {className}");
        return value;
    }

    internal static string AtomType(string atomsClass, string atomMember) =>
        HavokClassTypes.Shipped.Members(atomsClass).FirstOrDefault(m => m.Name == atomMember)?.CType
        ?? throw new InvalidDataException($"the class table has no {atomsClass}.{atomMember}");

    internal static int AtomOffset(string dataClass, string atomsClass, string atomMember, string member) =>
        Field(dataClass, "atoms") + Field(atomsClass, atomMember) + Field(AtomType(atomsClass, atomMember), member);
}

internal static class RagdollValues
{
    internal static float ReadHalf(PackfileObjects objects, int at, string what)
    {
        int? raw = objects.ReadNarrowAt(at, 2);
        if (raw is not int value)
            throw new InvalidDataException($"{what} lies outside the data section");

        return (float)BitConverter.UInt16BitsToHalf((ushort)value);
    }

    internal static bool TryHalf(float value, out float rounded)
    {
        rounded = 0f;
        if (!float.IsFinite(value)) return false;

        rounded = (float)(Half)value;
        return float.IsFinite(rounded);
    }

    internal static void WriteHalf(byte[] data, int at, float rounded) =>
        BitConverter.GetBytes(BitConverter.HalfToUInt16Bits((Half)rounded)).CopyTo(data, at);

    internal static bool Finite(Vector3 value) =>
        float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);
}
