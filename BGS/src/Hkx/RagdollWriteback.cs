using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;

namespace OpenCommonwealth.Services.Hkx;

internal sealed record RagdollChange(RagdollNode Edited, RagdollNode Original);

internal sealed record RagdollShapeChange(RagdollShape Edited, RagdollShape Original);

internal static class RagdollWriteback
{
    internal static byte[] Write(byte[] source, IReadOnlyList<RagdollNode> nodes,
                                 IReadOnlyList<RagdollShape> shapes,
                                 IReadOnlyList<RagdollChange> changed,
                                 IReadOnlyList<RagdollShapeChange> shapeChanged, bool force = false)
    {
        var image = PackfileImage.Read(source);
        NativeLayout.RequireWritable(image);

        var objects = new PackfileObjects(image);
        var mismatched = HavokClassTypes.Shipped.SignatureProblems(objects.ClassNames());
        if (mismatched.Count > 0)
            throw new InvalidOperationException(
                "This file's classes are not the ones this build describes, so nothing was written " +
                $"into its bytes: {mismatched[0]}");

        var data = image.Section(PackfileObjects.DataSection)
            ?? throw new InvalidOperationException("this file has no data section, so nothing was written");

        if (force && (changed.Count > 0 || shapeChanged.Count > 0))
            throw new InvalidOperationException("a forced no-op rebuild cannot carry edits, so nothing was written");

        var edits = force ? NoOpChanges(nodes) : changed;
        var shapeEdits = force ? NoOpShapeChanges(shapes) : shapeChanged;

        foreach (var change in edits) RequireArrays(image, objects, change.Edited);
        foreach (var change in shapeEdits) RequireShape(objects, change.Edited);

        var before = (byte[])data.Data.Clone();
        var ranges = new List<(int Start, int Length)>();

        foreach (var change in edits) WriteValues(data.Data, objects, change.Edited, change.Original, ranges);
        foreach (var change in shapeEdits) WriteShape(data.Data, objects, change.Edited, change.Original, ranges);

        RequireConfined(data.Data, before, ranges);
        RequireOtherInstancesIntact(before, data.Data, objects.Instances, ranges);

        var rebuilt = image.Rebuild();
        Verify(rebuilt, nodes, shapes, shapeEdits);
        return rebuilt;
    }

    private static List<RagdollChange> NoOpChanges(IReadOnlyList<RagdollNode> nodes)
    {
        var edits = new List<RagdollChange>(nodes.Count);
        foreach (var node in nodes) edits.Add(new RagdollChange(node, node.Copy()));
        return edits;
    }

    private static List<RagdollShapeChange> NoOpShapeChanges(IReadOnlyList<RagdollShape> shapes)
    {
        var edits = new List<RagdollShapeChange>(shapes.Count);
        foreach (var shape in shapes) edits.Add(new RagdollShapeChange(shape, shape.Copy()));
        return edits;
    }

    private static void RequireArrays(PackfileImage image, PackfileObjects objects, RagdollNode node)
    {
        RequireArray(image, objects, node.Instance, "materials", RagdollLayout.MaterialClass, node.Materials.Count);
        RequireArray(image, objects, node.Instance, "motionProperties", RagdollLayout.MotionPropertiesClass, -1);
        RequireArray(image, objects, node.Instance, "motionCinfos", RagdollLayout.MotionClass, node.Motions.Count);
        RequireArray(image, objects, node.Instance, "bodyCinfos", RagdollLayout.BodyClass, node.Bodies.Count);
        RequireArray(image, objects, node.Instance, "constraintCinfos", RagdollLayout.ConstraintCinfoClass,
                     node.Constraints.Count);
    }

    private static void RequireArray(PackfileImage image, PackfileObjects objects,
                                     PackfileObjects.Instance instance, string member, string elementClass,
                                     int count)
    {
        if (objects.FieldAt(instance, member) is not int header)
            throw new InvalidOperationException(
                $"{instance.ClassName} at 0x{instance.Offset:x} has no {member} member this build can place, " +
                "so nothing was written");

        int stride = RagdollLayout.Stride(elementClass);
        var run = objects.ArrayAt(header, stride)
            ?? throw new InvalidOperationException(
                $"{instance.ClassName} at 0x{instance.Offset:x} has {member} storage this build cannot locate, " +
                "so nothing was written");

        if (count >= 0 && run.Count != count)
            throw new InvalidDataException(
                $"{instance.ClassName} at 0x{instance.Offset:x} holds {run.Count} {member} row(s) where the " +
                $"model holds {count}: rows are never added or removed, so nothing was written");
        if (run.Count == 0) return;

        var data = image.Section(PackfileObjects.DataSection)
            ?? throw new InvalidOperationException("this file has no data section, so nothing was written");
        int section = image.Sections.IndexOf(data);

        long end = (long)run.At + (long)run.Count * stride;
        bool Inside(int at) => at >= run.At && at < end;

        bool shared = data.Locals().Any(entry => entry.Source != header && Inside(entry.Destination)) ||
                      data.Globals().Any(entry =>
                          entry.Source != header && entry.Section == section && Inside(entry.Destination)) ||
                      image.Sections.Where(owner => !ReferenceEquals(owner, data)).Any(owner =>
                          owner.Globals().Any(entry => entry.Section == section && Inside(entry.Destination))) ||
                      data.Virtuals().Any(entry => Inside(entry.Source));

        if (shared)
            throw new InvalidOperationException(
                $"{instance.ClassName} at 0x{instance.Offset:x}.{member} shares its row storage with another " +
                "object, so the edit was refused");
    }

    private static PackfileObjects.Instance RequireShape(PackfileObjects objects, RagdollShape shape)
    {
        if (shape.InstanceIndex < 0 || shape.InstanceIndex >= objects.Instances.Count)
            throw new InvalidOperationException(
                $"shape {shape.ObjectId} is not one of this file's objects, so nothing was written");

        var instance = objects.Instances[shape.InstanceIndex];
        if (instance.ClassName != shape.ClassName)
            throw new InvalidOperationException(
                $"shape {shape.ObjectId} is a {instance.ClassName} where the model holds a {shape.ClassName}, " +
                "so nothing was written");
        if (objects.FieldAt(instance, "convexRadius") is null || objects.FieldAt(instance, "a") is null ||
            objects.FieldAt(instance, "b") is null)
            throw new InvalidOperationException(
                $"{instance.ClassName} at 0x{instance.Offset:x} has no capsule fields this build can place, " +
                "so nothing was written");
        return instance;
    }

    private static (int At, int Count) Run(PackfileObjects objects, PackfileObjects.Instance instance,
                                           string member, string elementClass) =>
        RagdollRead.Run(objects, instance, member, elementClass)
        ?? throw new InvalidOperationException(
            $"{instance.ClassName} at 0x{instance.Offset:x} has {member} storage this build cannot locate, " +
            "so nothing was written");

    private static void WriteValues(byte[] data, PackfileObjects objects, RagdollNode edited,
                                    RagdollNode original, List<(int Start, int Length)> ranges)
    {
        RequireSameCounts(edited, original);

        int motionStride = RagdollLayout.Stride(RagdollLayout.MotionClass);
        int materialStride = RagdollLayout.Stride(RagdollLayout.MaterialClass);

        var motions = Run(objects, edited.Instance, "motionCinfos", RagdollLayout.MotionClass);
        var materials = Run(objects, edited.Instance, "materials", RagdollLayout.MaterialClass);

        int inverseMassAt = RagdollLayout.Field(RagdollLayout.MotionClass, "inverseMass");
        int inertiaAt = RagdollLayout.Field(RagdollLayout.MotionClass, "inverseInertiaLocal");
        int comAt = RagdollLayout.Field(RagdollLayout.MotionClass, "centerOfMassWorld");
        int dynamicAt = RagdollLayout.Field(RagdollLayout.MaterialClass, "dynamicFriction");
        int restitutionAt = RagdollLayout.Field(RagdollLayout.MaterialClass, "restitution");

        for (int i = 0; i < edited.Motions.Count; i++)
        {
            var now = edited.Motions[i];
            var was = original.Motions[i];
            int row = motions.At + i * motionStride;

            if (now.InverseMass != was.InverseMass)
            {
                WriteFloat(data, row + inverseMassAt, now.InverseMass);
                ranges.Add((row + inverseMassAt, 4));
            }

            if (now.InverseInertiaLocal != was.InverseInertiaLocal)
            {
                WriteVector(data, row + inertiaAt, now.InverseInertiaLocal);
                ranges.Add((row + inertiaAt, 12));
            }

            if (now.CenterOfMassWorld != was.CenterOfMassWorld)
            {
                WriteVector(data, row + comAt, now.CenterOfMassWorld);
                ranges.Add((row + comAt, 12));
            }
        }

        for (int i = 0; i < edited.Materials.Count; i++)
        {
            var now = edited.Materials[i];
            var was = original.Materials[i];
            int row = materials.At + i * materialStride;

            if (now.DynamicFriction != was.DynamicFriction || now.StaticFriction != was.StaticFriction)
            {
                RagdollValues.WriteHalf(data, row + dynamicAt, now.DynamicFriction);
                RagdollValues.WriteHalf(data, row + dynamicAt + 2, now.StaticFriction);
                ranges.Add((row + dynamicAt, 4));
            }

            if (now.Restitution != was.Restitution)
            {
                RagdollValues.WriteHalf(data, row + restitutionAt, now.Restitution);
                ranges.Add((row + restitutionAt, 2));
            }
        }

        for (int i = 0; i < edited.Constraints.Count; i++)
        {
            var now = edited.Constraints[i];
            var was = original.Constraints[i];
            WriteConstraint(data, objects, now, was, ranges);
        }
    }

    private static void RequireSameCounts(RagdollNode edited, RagdollNode original)
    {
        if (edited.Bodies.Count == original.Bodies.Count &&
            edited.Motions.Count == original.Motions.Count &&
            edited.Materials.Count == original.Materials.Count &&
            edited.Constraints.Count == original.Constraints.Count)
            return;

        throw new InvalidDataException(
            $"ragdoll #{edited.ObjectId} would change its row counts, and bodies, shapes and constraints are " +
            "never added or removed, so nothing was written");
    }

    private static void WriteConstraint(byte[] data, PackfileObjects objects, RagdollConstraint now,
                                        RagdollConstraint was, List<(int Start, int Length)> ranges)
    {
        bool friction = now.Friction != was.Friction;
        bool limits = now.MinAngle != was.MinAngle || now.MaxAngle != was.MaxAngle;
        bool cone = now.ConeMaxAngle != was.ConeMaxAngle;
        bool planes = now.PlaneMinAngle != was.PlaneMinAngle || now.PlaneMaxAngle != was.PlaneMaxAngle;
        if (!friction && !limits && !cone && !planes) return;

        if (!now.LimitsEditable || now.InstanceIndex != was.InstanceIndex)
            throw new InvalidOperationException(
                $"constraint {now.Index} of a ragdoll is a " +
                $"{(now.ClassName.Length > 0 ? now.ClassName : "constraint with no data object")}, whose atoms " +
                "this build does not write, so nothing was written");

        var instance = objects.Instances[now.InstanceIndex];
        if (instance.ClassName != now.ClassName)
            throw new InvalidOperationException(
                $"constraint {now.Index} is a {instance.ClassName} where the model holds a {now.ClassName}, " +
                "so nothing was written");

        bool ragdoll = now.IsRagdoll;

        if (friction)
        {
            int at = instance.Offset + RagdollLayout.AtomOffset(now.ClassName, AtomsClass(ragdoll), "angFriction",
                                                               "maxFrictionTorque");
            WriteFloat(data, at, now.Friction);
            ranges.Add((at, 4));
        }

        if (limits)
        {
            string atomsClass = AtomsClass(ragdoll);
            string atom = ragdoll ? "twistLimit" : "angLimit";
            int minAt = instance.Offset +
                        RagdollLayout.AtomOffset(now.ClassName, atomsClass, atom, "minAngle");
            int maxAt = instance.Offset +
                        RagdollLayout.AtomOffset(now.ClassName, atomsClass, atom, "maxAngle");
            WriteFloat(data, minAt, now.MinAngle);
            WriteFloat(data, maxAt, now.MaxAngle);
            ranges.Add((minAt, 4));
            ranges.Add((maxAt, 4));
        }

        if (cone)
        {
            if (!ragdoll)
                throw new InvalidOperationException(
                    $"constraint {now.Index} is a {now.ClassName}, which has no cone limit, so nothing was written");
            if (now.ConeAngleMeasurementMode != RagdollLayout.VerifiedConeAngleMode)
                throw new InvalidOperationException(
                    $"constraint {now.Index} stores cone angle measurement mode {now.ConeAngleMeasurementMode}, " +
                    $"where this build verifies mode {RagdollLayout.VerifiedConeAngleMode}, so nothing was written");

            int at = instance.Offset + RagdollLayout.AtomOffset(now.ClassName, AtomsClass(true), "coneLimit",
                                                                "maxAngle");
            WriteFloat(data, at, now.ConeMaxAngle);
            ranges.Add((at, 4));
        }

        if (planes)
        {
            if (!ragdoll)
                throw new InvalidOperationException(
                    $"constraint {now.Index} is a {now.ClassName}, which has no plane limit, so nothing was written");
            if (now.PlaneAngleMeasurementMode != RagdollLayout.VerifiedPlaneAngleMode)
                throw new InvalidOperationException(
                    $"constraint {now.Index} stores plane angle measurement mode {now.PlaneAngleMeasurementMode}, " +
                    $"where this build verifies mode {RagdollLayout.VerifiedPlaneAngleMode}, so nothing was written");

            int minAt = instance.Offset + RagdollLayout.AtomOffset(now.ClassName, AtomsClass(true), "planesLimit",
                                                                   "minAngle");
            int maxAt = instance.Offset + RagdollLayout.AtomOffset(now.ClassName, AtomsClass(true), "planesLimit",
                                                                   "maxAngle");
            WriteFloat(data, minAt, now.PlaneMinAngle);
            WriteFloat(data, maxAt, now.PlaneMaxAngle);
            ranges.Add((minAt, 4));
            ranges.Add((maxAt, 4));
        }
    }

    private static string AtomsClass(bool ragdoll) =>
        ragdoll ? RagdollLayout.RagdollAtomsClass : RagdollLayout.LimitedHingeAtomsClass;

    private static void WriteShape(byte[] data, PackfileObjects objects, RagdollShape edited,
                                   RagdollShape original, List<(int Start, int Length)> ranges)
    {
        var instance = RequireShape(objects, edited);
        bool geometry = edited.Radius != original.Radius || edited.A != original.A || edited.B != original.B;
        bool derived = !RagdollCapsule.SameData(edited.Capsule, original.Capsule);
        if (geometry || derived) RequireDerived(objects, instance, edited, original);

        int radiusAt = RagdollLayout.Field(RagdollLayout.ShapeClass, "convexRadius");
        int aAt = RagdollLayout.Field(RagdollLayout.CapsuleClass, "a");
        int bAt = RagdollLayout.Field(RagdollLayout.CapsuleClass, "b");

        if (edited.Radius != original.Radius)
        {
            WriteFloat(data, instance.Offset + radiusAt, edited.Radius);
            ranges.Add((instance.Offset + radiusAt, 4));
        }

        if (edited.A != original.A)
        {
            WriteVector(data, instance.Offset + aAt, edited.A);
            ranges.Add((instance.Offset + aAt, 12));
        }

        if (edited.B != original.B)
        {
            WriteVector(data, instance.Offset + bAt, edited.B);
            ranges.Add((instance.Offset + bAt, 12));
        }

        if (!derived) return;

        WritePayload(objects, instance, "vertices", RagdollCapsule.VertexWidth, RagdollCapsule.VertexCount,
                     edited.Capsule!.Vertices, original.Capsule!.Vertices, data, ranges);
        WritePayload(objects, instance, "planes", RagdollCapsule.PlaneWidth, RagdollCapsule.PlaneCount,
                     edited.Capsule.Planes, original.Capsule.Planes, data, ranges);
        WriteMassProperties(data, objects, instance, edited, original, ranges);
    }

    private static void RequireDerived(PackfileObjects objects, PackfileObjects.Instance instance,
                                       RagdollShape edited, RagdollShape original)
    {
        if (original.Capsule is null && edited.Capsule is null)
        {
            if (RagdollCapsule.MassPropertiesAt(objects, instance) != null)
                throw new InvalidOperationException(
                    $"shape {edited.ObjectId} carries mass properties but no polytope arrays this build can " +
                    "derive, so nothing was written");
            return;
        }

        if (original.Capsule is not { } before || edited.Capsule is not { } after)
            throw new InvalidOperationException(
                $"shape {edited.ObjectId} would gain or lose derived capsule arrays, and those are never added " +
                "or removed, so nothing was written");

        if (!RagdollCapsule.SameBytes(after.Faces, before.Faces) ||
            !RagdollCapsule.SameBytes(after.Indices, before.Indices))
            throw new InvalidOperationException(
                $"shape {edited.ObjectId} would change its faces or indices, which this build never derives, so " +
                "nothing was written");

        RagdollCapsuleData expected = RagdollCapsule.Derive(before, original.A, original.B, original.Radius,
                                                           edited.A, edited.B, edited.Radius,
                                                           $"shape {edited.ObjectId}", "so nothing was written");
        if (!RagdollCapsule.SameData(expected, after))
            throw new InvalidOperationException(
                $"shape {edited.ObjectId} would store derived arrays that a resize of its endpoints and radius " +
                "does not produce, so nothing was written");
    }

    private static void WritePayload(PackfileObjects objects, PackfileObjects.Instance instance, string member,
                                     int width, int count, byte[] produced, byte[] before, byte[] data,
                                     List<(int Start, int Length)> ranges)
    {
        if (RagdollCapsule.SameBytes(produced, before)) return;

        if (produced.Length != count * width || objects.FieldAt(instance, member) is not int header ||
            objects.RelArrayAt(header, width) is not { } run || run.Count != count || run.At < 0 ||
            run.At + produced.Length > data.Length)
            throw new InvalidOperationException(
                $"shape at 0x{instance.Offset:x} has {member} storage this build cannot write, so nothing was " +
                "written");

        produced.CopyTo(data, run.At);
        ranges.Add((run.At, produced.Length));
    }

    private static void WriteMassProperties(byte[] data, PackfileObjects objects, PackfileObjects.Instance instance,
                                            RagdollShape edited, RagdollShape original,
                                            List<(int Start, int Length)> ranges)
    {
        byte[]? now = edited.Capsule?.MassProperties;
        byte[]? was = original.Capsule?.MassProperties;
        if (RagdollCapsule.SameBytes(now, was)) return;

        if (now is null || was is null || now.Length != RagdollCapsule.MassPropertiesWidth ||
            was.Length != RagdollCapsule.MassPropertiesWidth ||
            RagdollCapsule.MassPropertiesAt(objects, instance) is not int at)
            throw new InvalidOperationException(
                $"shape {edited.ObjectId} would change mass properties the file does not hold, so nothing was " +
                "written");

        WriteSlice(data, at, now, was, 0, 8, ranges);
        WriteSlice(data, at, now, was, 8, 8, ranges);
        WriteSlice(data, at, now, was, 28, 4, ranges);
    }

    private static void WriteSlice(byte[] data, int at, byte[] now, byte[] was, int offset, int length,
                                   List<(int Start, int Length)> ranges)
    {
        if (now.AsSpan(offset, length).SequenceEqual(was.AsSpan(offset, length))) return;
        if (at < 0 || at + offset + length > data.Length)
            throw new InvalidOperationException(
                "a mass property field lies outside the data section, so nothing was written");

        now.AsSpan(offset, length).CopyTo(data.AsSpan(at + offset));
        ranges.Add((at + offset, length));
    }

    private static void WriteFloat(byte[] data, int at, float value)
    {
        if (at < 0 || at + 4 > data.Length)
            throw new InvalidOperationException("a ragdoll field lies outside the data section, so nothing was written");
        BitConverter.GetBytes(value).CopyTo(data, at);
    }

    private static void WriteVector(byte[] data, int at, Vector3 value)
    {
        if (at < 0 || at + 12 > data.Length)
            throw new InvalidOperationException("a ragdoll vector lies outside the data section, so nothing was written");
        BitConverter.GetBytes(value.X).CopyTo(data, at);
        BitConverter.GetBytes(value.Y).CopyTo(data, at + 4);
        BitConverter.GetBytes(value.Z).CopyTo(data, at + 8);
    }

    private static void RequireConfined(byte[] produced, byte[] before, IReadOnlyList<(int Start, int Length)> ranges)
    {
        if (produced.Length != before.Length)
            throw new InvalidDataException(
                $"a value edit produced {produced.Length} bytes where the section holds {before.Length}");

        for (int at = 0; at < produced.Length; at++)
        {
            if (produced[at] == before[at]) continue;
            if (ranges.Any(range => at >= range.Start && at < range.Start + range.Length)) continue;

            throw new InvalidDataException(
                $"a value edit reached byte 0x{at:x}, which is outside the field it was meant to write, " +
                "so nothing was written");
        }
    }

    private static void RequireOtherInstancesIntact(byte[] before, byte[] after,
                                                    IReadOnlyList<PackfileObjects.Instance> instances,
                                                    IReadOnlyList<(int Start, int Length)> ranges)
    {
        foreach (var instance in instances)
        {
            int? size = HavokClassTypes.Shipped[instance.ClassName]?.Size;
            if (size is not int span || span <= 0) continue;
            if (instance.Offset < 0 || instance.Offset + span > before.Length) continue;
            if (ranges.Any(range => instance.Offset < range.Start + range.Length &&
                                    range.Start < instance.Offset + span))
                continue;

            for (int i = 0; i < span; i++)
            {
                int at = instance.Offset + i;
                if (before[at] == after[at]) continue;

                throw new InvalidDataException(
                    $"a value edit reached byte 0x{at:x} inside {instance.ClassName} at 0x{instance.Offset:x}, " +
                    "which is another object instance, so nothing was written");
            }
        }
    }

    private static void Verify(byte[] produced, IReadOnlyList<RagdollNode> nodes, IReadOnlyList<RagdollShape> shapes,
                               IReadOnlyList<RagdollShapeChange> shapeChanged)
    {
        PackfileObjects objects;
        try
        {
            objects = new PackfileObjects(PackfileImage.Read(produced));
        }
        catch (Exception error)
        {
            throw new InvalidDataException("the written file could not be read back: " + error.Message, error);
        }

        var mismatched = HavokClassTypes.Shipped.SignatureProblems(objects.ClassNames());
        if (mismatched.Count > 0)
            throw new InvalidDataException("the written file's class signatures do not match this build: " + mismatched[0]);

        var readRagdolls = new List<RagdollNode>();
        var readShapes = new List<RagdollShape>();
        foreach (var instance in objects.Instances)
        {
            if (instance.ClassName == RagdollLayout.RagdollClass)
            {
                readRagdolls.Add(RagdollRead.Read(objects, instance)
                    ?? throw new InvalidDataException(
                        $"the written file's ragdoll at 0x{instance.Offset:x} did not read back, so nothing was written"));
                continue;
            }

            if (instance.ClassName != RagdollLayout.CapsuleClass) continue;
            readShapes.Add(RagdollRead.ReadShape(objects, instance)
                ?? throw new InvalidDataException(
                    $"the written file's shape at 0x{instance.Offset:x} did not read back, so nothing was written"));
        }

        if (readRagdolls.Count != nodes.Count)
            throw new InvalidDataException(
                $"the written file holds {readRagdolls.Count} readable ragdoll(s) where {nodes.Count} were " +
                "expected, so nothing was written");
        if (readShapes.Count != shapes.Count)
            throw new InvalidDataException(
                $"the written file holds {readShapes.Count} capsule shape(s) where {shapes.Count} were expected, " +
                "so nothing was written");

        for (int i = 0; i < nodes.Count; i++)
            if (!nodes[i].SameAs(readRagdolls[i]))
                throw new InvalidDataException(
                    $"ragdoll #{readRagdolls[i].ObjectId} did not read back as the model that was edited, " +
                    "so nothing was written");

        for (int i = 0; i < shapes.Count; i++)
            if (!shapes[i].SameAs(readShapes[i]))
                throw new InvalidDataException(
                    $"shape {readShapes[i].ObjectId} did not read back as the model that was edited, " +
                    "so nothing was written");

        foreach (var change in shapeChanged)
        {
            if (change.Original.Capsule is not { } before) continue;
            if (change.Edited.A == change.Original.A && change.Edited.B == change.Original.B &&
                change.Edited.Radius == change.Original.Radius &&
                RagdollCapsule.SameData(change.Edited.Capsule, change.Original.Capsule))
                continue;

            var read = readShapes.FirstOrDefault(shape => shape.InstanceIndex == change.Edited.InstanceIndex);
            if (read is null || read.Capsule is not { } after)
                throw new InvalidDataException(
                    $"the written file lost shape {change.Edited.ObjectId}'s derived arrays, so nothing was written");

            RagdollCapsuleData expected = RagdollCapsule.Derive(before, change.Original.A, change.Original.B,
                                                               change.Original.Radius, change.Edited.A,
                                                               change.Edited.B, change.Edited.Radius,
                                                               $"shape {change.Edited.ObjectId}",
                                                               "so nothing was written");
            if (!RagdollCapsule.SameData(expected, after))
                throw new InvalidDataException(
                    $"the written file's shape {change.Edited.ObjectId} did not read back as the polytope its " +
                    "endpoints and radius derive, so nothing was written");
        }
    }
}
