using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;

namespace OpenCommonwealth.Services.Hkx;

internal static class RagdollRead
{
    internal static (int At, int Count)? Run(PackfileObjects objects, PackfileObjects.Instance instance,
                                             string member, string elementClass)
    {
        if (objects.FieldAt(instance, member) is not int header) return null;

        int stride = RagdollLayout.Stride(elementClass);
        var run = objects.ArrayAt(header, stride);
        return run == null ? null : (run.At, run.Count);
    }

    internal static RagdollNode? Read(PackfileObjects objects, PackfileObjects.Instance instance)
    {
        int objectId = NativeGraphModel.FirstId + objects.IndexOf(instance);
        var node = new RagdollNode(instance, objectId);

        if (RagdollRead.Run(objects, instance, "materials", RagdollLayout.MaterialClass) is not { } materials)
            return null;
        if (RagdollRead.Run(objects, instance, "motionCinfos", RagdollLayout.MotionClass) is not { } motions)
            return null;
        if (RagdollRead.Run(objects, instance, "bodyCinfos", RagdollLayout.BodyClass) is not { } bodies)
            return null;
        if (RagdollRead.Run(objects, instance, "constraintCinfos", RagdollLayout.ConstraintCinfoClass)
            is not { } constraints)
            return null;
        if (RagdollRead.Run(objects, instance, "motionProperties", RagdollLayout.MotionPropertiesClass)
            is not { })
            return null;

        int materialStride = RagdollLayout.Stride(RagdollLayout.MaterialClass);
        int motionStride = RagdollLayout.Stride(RagdollLayout.MotionClass);
        int bodyStride = RagdollLayout.Stride(RagdollLayout.BodyClass);
        int constraintStride = RagdollLayout.Stride(RagdollLayout.ConstraintCinfoClass);

        int frictionAt = RagdollLayout.Field(RagdollLayout.MaterialClass, "dynamicFriction");
        int staticAt = RagdollLayout.Field(RagdollLayout.MaterialClass, "staticFriction");
        int restitutionAt = RagdollLayout.Field(RagdollLayout.MaterialClass, "restitution");
        int sharedAt = RagdollLayout.Field(RagdollLayout.MaterialClass, "isShared");

        int inverseMassAt = RagdollLayout.Field(RagdollLayout.MotionClass, "inverseMass");
        int inertiaAt = RagdollLayout.Field(RagdollLayout.MotionClass, "inverseInertiaLocal");
        int comAt = RagdollLayout.Field(RagdollLayout.MotionClass, "centerOfMassWorld");

        int motionIdAt = RagdollLayout.Field(RagdollLayout.BodyClass, "motionId");
        int materialIdAt = RagdollLayout.Field(RagdollLayout.BodyClass, "materialId");
        int nameAt = RagdollLayout.Field(RagdollLayout.BodyClass, "name");

        var materialRows = new List<RagdollMaterial>(materials.Count);
        for (int i = 0; i < materials.Count; i++)
        {
            int row = materials.At + i * materialStride;
            string what = $"ragdoll #{objectId}.materials[{i}]";
            var material = new RagdollMaterial(i)
            {
                DynamicFriction = RagdollValues.ReadHalf(objects, row + frictionAt, $"{what}.dynamicFriction"),
                StaticFriction = RagdollValues.ReadHalf(objects, row + staticAt, $"{what}.staticFriction"),
                Restitution = RagdollValues.ReadHalf(objects, row + restitutionAt, $"{what}.restitution"),
                FileSaysShared = objects.ByteAt(row + sharedAt) != 0,
            };
            materialRows.Add(material);
        }

        var motionRows = new List<RagdollMotion>(motions.Count);
        for (int i = 0; i < motions.Count; i++)
        {
            int row = motions.At + i * motionStride;
            float[] inertia = objects.ReadFloatsAt(row + inertiaAt, 4)
                ?? throw new InvalidDataException(
                    $"ragdoll #{objectId}.motionCinfos[{i}].inverseInertiaLocal lies outside the data section");
            float[] com = objects.ReadFloatsAt(row + comAt, 4)
                ?? throw new InvalidDataException(
                    $"ragdoll #{objectId}.motionCinfos[{i}].centerOfMassWorld lies outside the data section");

            motionRows.Add(new RagdollMotion(i)
            {
                InverseMass = objects.ReadFloatAt(row + inverseMassAt)
                    ?? throw new InvalidDataException(
                        $"ragdoll #{objectId}.motionCinfos[{i}].inverseMass lies outside the data section"),
                InverseInertiaLocal = new Vector3(inertia[0], inertia[1], inertia[2]),
                CenterOfMassWorld = new Vector3(com[0], com[1], com[2]),
            });
        }

        var bodyRows = new List<RagdollBody>(bodies.Count);
        for (int i = 0; i < bodies.Count; i++)
        {
            int row = bodies.At + i * bodyStride;
            int motionId = objects.ReadNarrowAt(row + motionIdAt, 4)
                ?? throw new InvalidDataException($"ragdoll #{objectId}.bodyCinfos[{i}].motionId is unreadable");
            int materialId = objects.ReadNarrowAt(row + materialIdAt, 2)
                ?? throw new InvalidDataException($"ragdoll #{objectId}.bodyCinfos[{i}].materialId is unreadable");

            int shape = -1;
            var target = objects.ReadRefAt(row, out bool wasNull);
            if (!wasNull && target != null) shape = NativeGraphModel.FirstId + objects.IndexOf(target);

            bodyRows.Add(new RagdollBody(i, motionId, materialId, shape, objects.ReadStringAt(row + nameAt) ?? ""));
        }

        foreach (var material in materialRows)
            material.ReferencedByBodies = bodyRows.Count(body => body.MaterialId == material.Index);

        var constraintRows = new List<RagdollConstraint>(constraints.Count);
        for (int i = 0; i < constraints.Count; i++)
        {
            int row = constraints.At + i * constraintStride;
            var data = objects.ReadRefAt(row, out bool wasNull);
            if (wasNull) data = null;

            string className = data?.ClassName ?? "";
            int instanceIndex = data == null ? -1 : objects.IndexOf(data);
            var constraint = new RagdollConstraint(i, data == null ? -1 : NativeGraphModel.FirstId + instanceIndex,
                                                   instanceIndex, className);
            if (data != null) ReadConstraintValues(objects, data, constraint, objectId, i);
            constraintRows.Add(constraint);
        }

        node.Materials = materialRows;
        node.Motions = motionRows;
        node.Bodies = bodyRows;
        node.Constraints = constraintRows;
        node.BoneToBodyMap = ReadBoneToBodyMap(objects, instance);
        node.Skeleton = ReadSkeleton(objects, instance);
        return node;
    }

    private static void ReadConstraintValues(PackfileObjects objects, PackfileObjects.Instance data,
                                             RagdollConstraint constraint, int objectId, int index)
    {
        if (!constraint.LimitsEditable) return;

        string dataClass = constraint.ClassName;
        string atomsClass = constraint.IsRagdoll
            ? RagdollLayout.RagdollAtomsClass
            : RagdollLayout.LimitedHingeAtomsClass;

        constraint.Friction = Atom(objects, data, dataClass, atomsClass, "angFriction", "maxFrictionTorque",
                                   objectId, index);
        if (constraint.IsRagdoll)
        {
            constraint.MinAngle = Atom(objects, data, dataClass, atomsClass, "twistLimit", "minAngle", objectId, index);
            constraint.MaxAngle = Atom(objects, data, dataClass, atomsClass, "twistLimit", "maxAngle", objectId, index);
            constraint.ConeMaxAngle = Atom(objects, data, dataClass, atomsClass, "coneLimit", "maxAngle", objectId, index);
            constraint.PlaneMinAngle = Atom(objects, data, dataClass, atomsClass, "planesLimit", "minAngle", objectId, index);
            constraint.PlaneMaxAngle = Atom(objects, data, dataClass, atomsClass, "planesLimit", "maxAngle", objectId, index);
            constraint.ConeAngleMeasurementMode = Mode(objects, data, dataClass, atomsClass, "coneLimit");
            constraint.ConeMinAngle = Atom(objects, data, dataClass, atomsClass, "coneLimit", "minAngle", objectId, index);
            constraint.PlaneAngleMeasurementMode = Mode(objects, data, dataClass, atomsClass, "planesLimit");
            return;
        }

        constraint.MinAngle = Atom(objects, data, dataClass, atomsClass, "angLimit", "minAngle", objectId, index);
        constraint.MaxAngle = Atom(objects, data, dataClass, atomsClass, "angLimit", "maxAngle", objectId, index);
    }

    private static int Mode(PackfileObjects objects, PackfileObjects.Instance data, string dataClass,
                            string atomsClass, string atomMember) =>
        objects.ReadNarrowAt(data.Offset +
                             RagdollLayout.AtomOffset(dataClass, atomsClass, atomMember, "angleMeasurementMode"), 1) ?? -1;

    private static float Atom(PackfileObjects objects, PackfileObjects.Instance data, string dataClass,
                              string atomsClass, string atomMember, string member, int objectId, int index) =>
        objects.ReadFloatAt(data.Offset +
                            RagdollLayout.AtomOffset(dataClass, atomsClass, atomMember, member))
        ?? throw new InvalidDataException(
            $"ragdoll #{objectId}.constraintCinfos[{index}].{atomMember}.{member} lies outside the data section");

    private static IReadOnlyList<int> ReadBoneToBodyMap(PackfileObjects objects, PackfileObjects.Instance instance)
    {
        if (objects.FieldAt(instance, "boneToBodyMap") is not int header) return Array.Empty<int>();

        var run = objects.ArrayAt(header, 4);
        if (run == null) return Array.Empty<int>();

        var values = new List<int>(run.Count);
        for (int i = 0; i < run.Count; i++)
        {
            if (objects.ReadIntAt(run.At + i * 4) is not int value) return Array.Empty<int>();
            values.Add(value);
        }
        return values;
    }

    private static HavokRagdollSkeleton? ReadSkeleton(PackfileObjects objects, PackfileObjects.Instance instance)
    {
        var target = objects.ReadRefAt(objects.FieldAt(instance, "skeleton") ?? -1, out bool wasNull);
        return wasNull || target == null ? null : SkeletonMapperReader.ReadSkeleton(objects, target);
    }

    internal static RagdollShape? ReadShape(PackfileObjects objects, PackfileObjects.Instance instance)
    {
        int index = objects.IndexOf(instance);
        if (index < 0) return null;

        var shape = new RagdollShape(index, NativeGraphModel.FirstId + index, instance.ClassName);
        if (objects.FieldAt(instance, "convexRadius") is int radiusAt && objects.ReadFloatAt(radiusAt) is float radius)
            shape.Radius = radius;
        else
            return null;

        if (objects.FieldAt(instance, "a") is int aAt && objects.ReadFloatsAt(aAt, 4) is { } a)
            shape.A = new Vector3(a[0], a[1], a[2]);
        else
            return null;

        if (objects.FieldAt(instance, "b") is int bAt && objects.ReadFloatsAt(bAt, 4) is { } b)
            shape.B = new Vector3(b[0], b[1], b[2]);
        else
            return null;

        int vertexWidth = HavokClassTypes.Width("TYPE_VECTOR4");
        int faceWidth = RagdollLayout.Stride(RagdollLayout.PolytopeFaceClass);
        byte[]? vertices = PolytopeBytes(objects, instance, "vertices", vertexWidth);
        byte[]? planes = PolytopeBytes(objects, instance, "planes", vertexWidth);
        byte[]? faces = PolytopeBytes(objects, instance, "faces", faceWidth);
        byte[]? indices = PolytopeBytes(objects, instance, "indices", HavokClassTypes.Width("TYPE_UINT8"));
        if (vertices == null || planes == null || faces == null || indices == null) return null;

        shape.Vertices = vertices.Length / vertexWidth;
        shape.Planes = planes.Length / vertexWidth;
        shape.Faces = faces.Length / faceWidth;
        shape.Indices = indices.Length;

        byte[]? massProperties = MassProperties(objects, instance, shape);
        if (shape.CarriesPolytopeData || massProperties != null)
            shape.Capsule = new RagdollCapsuleData(vertices, planes, faces, indices, massProperties);
        return shape;
    }

    private static byte[]? MassProperties(PackfileObjects objects, PackfileObjects.Instance instance,
                                          RagdollShape shape)
    {
        if (RagdollCapsule.MassPropertiesSiteAt(objects, instance) is not { } site) return null;
        shape.PropertiesIndex = site.PropertiesIndex;
        shape.MassPropertiesIndex = site.MassPropertiesIndex;
        if (site.At < 0 || site.At + RagdollCapsule.MassPropertiesWidth > objects.DataLength) return null;

        var bytes = new byte[RagdollCapsule.MassPropertiesWidth];
        for (int i = 0; i < bytes.Length; i++) bytes[i] = objects.ByteAt(site.At + i);
        return bytes;
    }

    private static byte[]? PolytopeBytes(PackfileObjects objects, PackfileObjects.Instance instance, string member,
                                         int width)
    {
        if (objects.FieldAt(instance, member) is not int header) return null;
        if (objects.RelArrayAt(header, width) is not { } run) return null;

        int length = run.Count * width;
        if (length < 0 || run.At < 0 || run.At + length > objects.DataLength) return null;

        var bytes = new byte[length];
        for (int i = 0; i < length; i++) bytes[i] = objects.ByteAt(run.At + i);
        return bytes;
    }
}
