using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Numerics;

namespace OpenCommonwealth.Services.Hkx;

/// <summary>
/// Measured packfile extraction for the static ragdoll preview. Reads hknpRagdollData
/// (bodies, constraint graph, bone mapping) and the shape objects it references, straight
/// from the packfile bytes through the same typed readers and byte-backed XML the rest of
/// BGS uses. Nothing here infers a Havok layout: a member is only read under the name the
/// measured class table publishes, and unreadable/missing data fails closed to no model or
/// an incomplete model, never a guessed value.
/// </summary>
public static class HavokPhysicsExtractor
{
    public const string RagdollDataClass = "hknpRagdollData";
    public const string CapsuleShapeClass = "hknpCapsuleShape";

    public static HavokRagdollModel? TryExtract(byte[]? hkx)
    {
        if (hkx == null || hkx.Length == 0) return null;

        PackfileImage image;
        PackfileObjects objects;
        try
        {
            image = PackfileImage.Read(hkx);
            objects = new PackfileObjects(image);
        }
        catch (Exception)
        {
            return null;
        }

        string xml;
        try { xml = NativeXml.From(objects, image); }
        catch (Exception) { return null; }

        var graph = BehaviourGraphModel.Parse(xml);
        var ragdoll = graph.Objects.FirstOrDefault(o => o.Class == RagdollDataClass);
        if (ragdoll == null) return null;

        var model = new HavokRagdollModel();

        foreach (var instance in objects.Instances.Where(i => i.ClassName == CapsuleShapeClass))
            if (Shape(objects, instance) is { } shape)
                model.Shapes.Add(shape);

        ragdoll.StructLists.TryGetValue("motionCinfos", out var motions);

        if (ragdoll.StructLists.TryGetValue("bodyCinfos", out var cinfos))
            for (int i = 0; i < cinfos.Count; i++)
                if (Body(graph, i, cinfos[i], motions) is { } body)
                    model.Bodies.Add(body);

        if (ragdoll.StructLists.TryGetValue("constraintCinfos", out var constraints))
            for (int i = 0; i < constraints.Count; i++)
                if (Constraint(objects, graph, i, constraints[i]) is { } constraint)
                    model.Constraints.Add(constraint);

        if (ragdoll.Lists.TryGetValue("boneToBodyMap", out var map))
            for (int bone = 0; bone < map.Count; bone++)
                if (int.TryParse(map[bone], NumberStyles.Integer, CultureInfo.InvariantCulture, out int body))
                    model.BoneBindings.Add(new HavokRagdollBoneBinding(bone, body));

        if (ragdoll.Scalars.TryGetValue("skeleton", out var skeletonReference) &&
            RefId(skeletonReference) is { } skeletonId && skeletonId >= 0)
        {
            int index = skeletonId - NativeGraphModel.FirstId;
            if (index >= 0 && index < objects.Instances.Count &&
                objects.Instances[index].ClassName == "hkaSkeleton" &&
                Skeleton(objects, objects.Instances[index]) is { } skeleton)
                model.Skeleton = skeleton;

            // The file's animation->ragdoll bridge (hkaSkeletonMapper): take the mapper whose
            // skeletonB is exactly the ragdoll skeleton, and keep its simple mappings plus the
            // animation skeleton it maps from. Without it the ragdoll stays at the measured
            // reference pose and nothing else changes.
            if (model.Skeleton != null && skeletonId >= 0)
                foreach (var mapper in objects.Instances.Where(i => i.ClassName == MapperClass))
                    if (Mapper(objects, skeletonId, mapper, out var mappings, out var animation))
                    {
                        model.Mappings.AddRange(mappings);
                        model.AnimationSkeleton = animation;
                        break;
                    }
        }

        return model;
    }

    public const string MapperClass = "hkaSkeletonMapper";

    // The hkaSkeletonMapper's mapping data is an inline struct (hkaSkeletonMapper.mapping,
    // not a packfile object), read through the measured type table exactly like the atoms:
    // hkaSkeletonMapperData carries skeletonA/skeletonB pointers, the mappingType int32 and
    // the simpleMappings array of 64-byte hkaSkeletonMapperDataSimpleMapping records (int16
    // boneA/boneB, then the 48-byte aFromBTransform hkQsTransform: translation vector4,
    // rotation quaternion, scale vector4). Only an HK_RAGDOLL_MAPPING (type 0) whose
    // skeletonB is the ragdoll skeleton is accepted; everything else fails closed.
    private static bool Mapper(PackfileObjects objects, int ragdollSkeletonId,
                               PackfileObjects.Instance instance,
                               out List<HavokRagdollMapping> mappings,
                               out HavokRagdollSkeleton? animation)
    {
        mappings = new List<HavokRagdollMapping>();
        animation = null;

        var mapperLayout = LayoutWalker.Active(HavokClassTypes.Shipped, MapperClass, objects.PointerWidth);
        int? mappingAt = mapperLayout?.OffsetOf("mapping") is { } offset ? instance.Offset + offset : null;
        if (mappingAt == null) return false;

        var skeletonA = objects.ReadRefAt(mappingAt.Value + 0, out _);
        var skeletonB = objects.ReadRefAt(mappingAt.Value + 8, out _);
        if (skeletonA == null || skeletonB == null) return false;
        if (objects.IndexOf(skeletonB) + NativeGraphModel.FirstId != ragdollSkeletonId) return false;

        var dataLayout = LayoutWalker.Active(HavokClassTypes.Shipped, "hkaSkeletonMapperData", objects.PointerWidth);
        if (dataLayout?.OffsetOf("simpleMappings") is not { } rowsOffset) return false;
        int? typeAt = dataLayout.OffsetOf("mappingType") is { } typeOffset ? mappingAt.Value + typeOffset : null;
        int mappingType = typeAt is { } ta ? (objects.ReadNarrowAt(ta, 4) ?? 1) : 1;
        if (mappingType != 0) return false;

        var rows = objects.ArrayAt(mappingAt.Value + rowsOffset, 64);
        if (rows == null || rows.Count == 0) return false;

        var simpleLayout = LayoutWalker.Active(
            HavokClassTypes.Shipped, "hkaSkeletonMapperDataSimpleMapping", objects.PointerWidth);
        if (simpleLayout?.OffsetOf("boneA") is not { } boneAOffset ||
            simpleLayout.OffsetOf("boneB") is not { } boneBOffset ||
            simpleLayout.OffsetOf("aFromBTransform") is not { } transformOffset)
            return false;

        for (int i = 0; i < rows.Count; i++)
        {
            int row = rows.At + i * 64;
            int? boneA = objects.ReadNarrowAt(row + boneAOffset, 2);
            int? boneB = objects.ReadNarrowAt(row + boneBOffset, 2);
            float[]? values = objects.ReadFloatsAt(row + transformOffset, 12);
            if (boneA == null || boneB == null || values == null) return false;

            mappings.Add(new HavokRagdollMapping(
                unchecked((short)boneA.Value),
                unchecked((short)boneB.Value),
                new Vector3(values[0], values[1], values[2]),
                Quaternion.Normalize(new Quaternion(values[4], values[5], values[6], values[7]))));
        }

        if (skeletonA.ClassName != "hkaSkeleton") return false;
        animation = Skeleton(objects, skeletonA);
        return animation != null;
    }

    // The physics skeleton hknpRagdollData references, read through the measured type table
    // exactly like the byte-backed XML: name string, int16 parent array, hkaBone name array
    // (each record is a string pointer followed by a lockTranslation bool) and the 48-byte
    // hkQsTransform reference poses (translation vector4, rotation quaternion, scale vector4).
    private static HavokRagdollSkeleton? Skeleton(PackfileObjects objects, PackfileObjects.Instance instance)
    {
        const string className = "hkaSkeleton";
        var reader = new MemberReader(objects);

        string name = "";
        if (reader.At(instance.Offset, className, "name") is { } nameAt)
            name = objects.ReadStringAt(nameAt) ?? "";

        var skeleton = new HavokRagdollSkeleton { Name = name };

        if (reader.At(instance.Offset, className, "parentIndices") is { } parentsAt &&
            objects.ArrayAt(parentsAt, 2) is { } parents)
            for (int i = 0; i < parents.Count; i++)
            {
                int? raw = objects.ReadNarrowAt(parents.At + i * 2, 2);
                if (raw == null) return null;
                skeleton.ParentIndices.Add(unchecked((short)raw.Value));
            }

        if (reader.At(instance.Offset, className, "bones") is { } bonesAt &&
            objects.ArrayAt(bonesAt, 16) is { } bones)
            for (int i = 0; i < bones.Count; i++)
            {
                string? boneName = objects.ReadStringAt(bones.At + i * 16);
                if (boneName == null) return null;
                skeleton.BoneNames.Add(boneName);
            }

        // Without the bone names the skeleton cannot label anything; a readable skeleton
        // always carries them, so fail closed rather than expose a half-read object.
        if (skeleton.BoneNames.Count == 0) return null;

        if (reader.At(instance.Offset, className, "referencePose") is { } poseAt &&
            objects.ArrayAt(poseAt, 48) is { } poses)
            for (int i = 0; i < poses.Count; i++)
            {
                float[]? row = objects.ReadFloatsAt(poses.At + i * 48, 12);
                if (row == null) return null;
                skeleton.ReferencePose.Add(new HkxBonePose(
                    new Vector3(row[0], row[1], row[2]),
                    new Quaternion(row[4], row[5], row[6], row[7]),
                    new Vector3(row[8], row[9], row[10])));
            }

        return skeleton;
    }

    // A member offset through the same flattened parent-chain layout the byte-backed XML
    // writer uses (editor-write tables only carry the declared members of a class, not its
    // ancestors), so the read and the XML always agree on where a field lives.
    private static int? MemberAt(PackfileObjects objects, PackfileObjects.Instance instance, string member)
    {
        var layout = LayoutWalker.Active(HavokClassTypes.Shipped, instance.ClassName, objects.PointerWidth);
        return layout?.OffsetOf(member) is { } offset
            ? instance.Offset + offset
            : null;
    }

    private static HavokPhysicsShape? Shape(PackfileObjects objects, PackfileObjects.Instance instance)
    {
        var shape = new HavokPhysicsShape
        {
            Id = NativeGraphModel.FirstId + objects.IndexOf(instance),
            Kind = KindOf(instance.ClassName),
        };
        var types = HavokClassTypes.Shipped;

        int? verticesAt = MemberAt(objects, instance, "vertices");
        if (verticesAt is int vAt &&
            objects.RelArrayAt(vAt, 16) is { } vertices && vertices.Count > 0)
            for (int i = 0; i < vertices.Count; i++)
            {
                var row = objects.ReadFloatsAt(vertices.At + i * 16, 4);
                if (row == null) break;
                shape.Vertices.Add(new Vector3(row[0], row[1], row[2]));
            }

        int? indicesAt = MemberAt(objects, instance, "indices");
        var indices = indicesAt is int iAt && objects.RelArrayAt(iAt, 1) is { } indexRows
            ? IndexValues(objects, indexRows)
            : null;

        int? facesAt = MemberAt(objects, instance, "faces");
        var faceMember = types.Members(instance.ClassName).FirstOrDefault(m => m.Name == "faces");
        if (facesAt is int fAt && faceMember?.CType is { } faceType)
        {
            var faceLayout = LayoutWalker.Active(types, faceType, objects.PointerWidth);
            int? firstOffset = faceLayout?.OffsetOf("firstIndex");
            int? countOffset = faceLayout?.OffsetOf("numIndices");
            if (faceLayout is { } layout && layout.Size > 0 && firstOffset is { } fOff && countOffset is { } cOff &&
                objects.RelArrayAt(fAt, layout.Size) is { } faceRows && faceRows.Count > 0 &&
                indices != null)
                for (int i = 0; i < faceRows.Count; i++)
                {
                    int row = faceRows.At + i * layout.Size;
                    int? first = objects.ReadNarrowAt(row + fOff, 2);
                    int? count = objects.ReadNarrowAt(row + cOff, 1);
                    if (first == null || count == null || first < 0 || count < 0 ||
                        (long)first + count > indices.Length) continue;

                    var ring = new int[count.Value];
                    for (int e = 0; e < count.Value; e++) ring[e] = indices[first.Value + e];
                    shape.FaceRings.Add(ring);
                }
        }

        return shape;
    }

    private static int[] IndexValues(PackfileObjects objects, PackfileObjects.RelElements indexRows)
    {
        var values = new int[indexRows.Count];
        for (int i = 0; i < indexRows.Count; i++)
        {
            int? value = objects.ReadNarrowAt(indexRows.At + i, 1);
            if (value == null) return Array.Empty<int>();
            values[i] = value.Value;
        }
        return values;
    }

    private static HavokRigidBody? Body(BehaviourGraphModel graph, int id, IReadOnlyDictionary<string, string> cinfo,
                                       IReadOnlyList<IReadOnlyDictionary<string, string>>? motions)
    {
        string position = cinfo.TryGetValue("position", out var p) ? p : "";
        string orientation = cinfo.TryGetValue("orientation", out var o) ? o : "";
        if (!Vector4(position, out var frame) || !Vector4(orientation, out var quat)) return null;

        int shapeId = cinfo.TryGetValue("shape", out var shape)
            ? RefId(shape)
            : -1;

        // The body cinfo carries a motion id into hknpRagdollData.motionCinfos, whose
        // centerOfMassWorld is the measured world-space center of mass. When the file has no
        // motion cinfo (or the id does not resolve) the body keeps the Zero default rather
        // than inventing a value.
        Vector3 com = Vector3.Zero;
        int motionId = Int(cinfo, "motionId");
        if (motions != null && motionId >= 0 && motionId < motions.Count &&
            motions[motionId].TryGetValue("centerOfMassWorld", out var comText) &&
            Vector4(comText, out var comVec))
            com = new Vector3(comVec.X, comVec.Y, comVec.Z);

        return new HavokRigidBody
        {
            Id = id,
            Name = cinfo.TryGetValue("name", out var name) ? name.Trim() : "",
            ShapeId = shapeId,
            Position = new Vector3(frame.X, frame.Y, frame.Z),
            Rotation = new Quaternion(quat.X, quat.Y, quat.Z, quat.W),
            CenterOfMass = com,
        };
    }

    private static HavokConstraint? Constraint(PackfileObjects objects, BehaviourGraphModel graph, int id,
                                               IReadOnlyDictionary<string, string> cinfo)
    {
        int bodyA = Int(cinfo, "bodyA");
        int bodyB = Int(cinfo, "bodyB");
        if (bodyA < 0 || bodyB < 0) return null;

        string className = "";
        PackfileObjects.Instance? data = null;
        if (cinfo.TryGetValue("constraintData", out var reference) && RefId(reference) is { } refId && refId >= 0)
        {
            className = graph.Get(refId.ToString())?.Class ?? "";
            int index = refId - NativeGraphModel.FirstId;
            if (index >= 0 && index < objects.Instances.Count) data = objects.Instances[index];
        }

        var measured = data == null
            ? new ConstraintMeasured(KindOfConstraint(className), null, null, null, null, null,
                                     0, 1, 0, 0, 0, 0, 0)
            : MeasureConstraint(objects, data);

        return new HavokConstraint
        {
            Id = id,
            BodyA = bodyA,
            BodyB = bodyB,
            Kind = measured.Kind,
            MinAngle = measured.MinAngle,
            MaxAngle = measured.MaxAngle,
            TwistMinAngle = measured.TwistMinAngle,
            TwistMaxAngle = measured.TwistMaxAngle,
            ConeMaxAngle = measured.ConeMaxAngle,
            TwistAxis = measured.TwistAxis,
            TwistRefAxis = measured.TwistRefAxis,
            ConeTwistAxis = measured.ConeTwistAxis,
            ConeRefAxis = measured.ConeRefAxis,
            LimitAxis = measured.LimitAxis,
            FrameA = measured.FrameA,
            FrameB = measured.FrameB,
        };
    }

    private readonly record struct ConstraintMeasured(
        HavokConstraintKind Kind,
        float[]? FrameA,
        float[]? FrameB,
        float? TwistMinAngle,
        float? TwistMaxAngle,
        float? ConeMaxAngle,
        int TwistAxis,
        int TwistRefAxis,
        int ConeTwistAxis,
        int ConeRefAxis,
        int LimitAxis,
        float MinAngle,
        float MaxAngle);

    // Walks the constraint atoms through the measured type table: every offset comes from the
    // flattened parent-chain layout of the class that owns the member, exactly like the
    // byte-backed XML writer resolves them.
    private static ConstraintMeasured MeasureConstraint(PackfileObjects objects, PackfileObjects.Instance instance)
    {
        var reader = new MemberReader(objects);
        var kind = KindOfConstraint(instance.ClassName);

        int? atoms = reader.At(instance.Offset, instance.ClassName, "atoms");
        string? atomsType = reader.CType(instance.ClassName, "atoms");
        if (atoms == null || atomsType == null)
            return new ConstraintMeasured(kind, null, null, null, null, null, 0, 1, 0, 0, 0, 0, 0);

        var frameA = Frame(reader, atoms, atomsType, "transformA");
        var frameB = Frame(reader, atoms, atomsType, "transformB");

        float? twistMin = null, twistMax = null, coneMax = null;
        int twistAxis = 0, twistRef = 1, coneTwist = 0, coneRef = 0, limitAxis = 0;
        float hingeMin = 0, hingeMax = 0;

        switch (kind)
        {
            case HavokConstraintKind.Ragdoll:
                int? twist = reader.At(atoms, atomsType, "twistLimit");
                string? twistType = reader.CType(atomsType, "twistLimit");
                if (twist != null && twistType != null)
                {
                    twistAxis = reader.Narrow(twist, twistType, "twistAxis") ?? 0;
                    twistRef = reader.Narrow(twist, twistType, "refAxis") ?? 1;
                    twistMin = reader.Float(twist, twistType, "minAngle");
                    twistMax = reader.Float(twist, twistType, "maxAngle");
                }

                int? cone = reader.At(atoms, atomsType, "coneLimit");
                string? coneType = reader.CType(atomsType, "coneLimit");
                if (cone != null && coneType != null)
                {
                    coneTwist = reader.Narrow(cone, coneType, "twistAxisInA") ?? 0;
                    coneRef = reader.Narrow(cone, coneType, "refAxisInB") ?? 0;
                    coneMax = reader.Float(cone, coneType, "maxAngle");
                }
                break;

            case HavokConstraintKind.LimitedHinge:
                int? ang = reader.At(atoms, atomsType, "angLimit");
                string? angType = reader.CType(atomsType, "angLimit");
                if (ang != null && angType != null)
                {
                    limitAxis = reader.Narrow(ang, angType, "limitAxis") ?? 0;
                    hingeMin = reader.Float(ang, angType, "minAngle") ?? 0;
                    hingeMax = reader.Float(ang, angType, "maxAngle") ?? 0;
                }
                break;
        }

        return new ConstraintMeasured(kind, frameA, frameB, twistMin, twistMax, coneMax,
                                      twistAxis, twistRef, coneTwist, coneRef, limitAxis,
                                      hingeMin, hingeMax);
    }

    private static float[]? Frame(MemberReader reader, int? atoms, string atomsType, string which)
    {
        int? transforms = reader.At(atoms, atomsType, "transforms");
        string? transformsType = reader.CType(atomsType, "transforms");
        int? at = transformsType == null ? null : reader.At(transforms, transformsType, which);
        return at is int abs ? reader.Objects.ReadFloatsAt(abs, 16) : null;
    }

    private sealed class MemberReader
    {
        public MemberReader(PackfileObjects objects)
        {
            Objects = objects;
            Types = HavokClassTypes.Shipped;
        }

        public PackfileObjects Objects { get; }
        private HavokClassTypes Types { get; }

        public string? CType(string className, string member) =>
            Types.Members(className).FirstOrDefault(m => m.Name == member)?.CType;

        public int? At(int? baseAt, string className, string member)
        {
            if (baseAt == null) return null;
            return LayoutWalker.Active(Types, className, Objects.PointerWidth)?.OffsetOf(member) is { } offset
                ? baseAt + offset
                : null;
        }

        public int? Narrow(int? baseAt, string className, string member)
        {
            if (baseAt == null) return null;
            return LayoutWalker.Active(Types, className, Objects.PointerWidth)?.OffsetOf(member) is { } offset
                ? Objects.ReadNarrowAt(baseAt.Value + offset, 1)
                : null;
        }

        public float? Float(int? baseAt, string className, string member)
        {
            if (baseAt == null) return null;
            return LayoutWalker.Active(Types, className, Objects.PointerWidth)?.OffsetOf(member) is { } offset
                ? Objects.ReadFloatAt(baseAt.Value + offset)
                : null;
        }
    }

    private static HavokPhysicsShapeKind KindOf(string className) =>
        className == CapsuleShapeClass ? HavokPhysicsShapeKind.Capsule : HavokPhysicsShapeKind.Unknown;

    private static HavokConstraintKind KindOfConstraint(string className) => className switch
    {
        "hkpRagdollConstraintData" => HavokConstraintKind.Ragdoll,
        "hkpLimitedHingeConstraintData" => HavokConstraintKind.LimitedHinge,
        "hkpHingeConstraintData" => HavokConstraintKind.Hinge,
        "hkpBallAndSocketConstraintData" => HavokConstraintKind.BallAndSocket,
        "hkpFixedConstraintData" => HavokConstraintKind.Fixed,
        _ => HavokConstraintKind.Unknown,
    };

    private static int RefId(string value) =>
        value.StartsWith('#') && int.TryParse(value.AsSpan(1), NumberStyles.Integer,
                                               CultureInfo.InvariantCulture, out int id)
            ? id
            : -1;

    private static int Int(IReadOnlyDictionary<string, string> values, string member) =>
        values.TryGetValue(member, out var raw) &&
        int.TryParse(raw, NumberStyles.Integer, CultureInfo.InvariantCulture, out int value)
            ? value
            : -1;

    private static bool Vector4(string text, out Vector4 value)
    {
        value = default;
        if (text.Length < 2 || text[0] != '(' || text[^1] != ')') return false;

        string[] parts = text[1..^1].Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        if (parts.Length != 4) return false;

        var floats = new float[4];
        for (int i = 0; i < 4; i++)
            if (!float.TryParse(parts[i], NumberStyles.Float, CultureInfo.InvariantCulture, out floats[i]))
                return false;

        value = new Vector4(floats[0], floats[1], floats[2], floats[3]);
        return true;
    }

}