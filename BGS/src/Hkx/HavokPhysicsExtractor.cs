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

        if (ragdoll.StructLists.TryGetValue("bodyCinfos", out var cinfos))
            for (int i = 0; i < cinfos.Count; i++)
                if (Body(graph, i, cinfos[i]) is { } body)
                    model.Bodies.Add(body);

        if (ragdoll.StructLists.TryGetValue("constraintCinfos", out var constraints))
            for (int i = 0; i < constraints.Count; i++)
                if (Constraint(graph, i, constraints[i]) is { } constraint)
                    model.Constraints.Add(constraint);

        if (ragdoll.Lists.TryGetValue("boneToBodyMap", out var map))
            for (int bone = 0; bone < map.Count; bone++)
                if (int.TryParse(map[bone], NumberStyles.Integer, CultureInfo.InvariantCulture, out int body))
                    model.BoneBindings.Add(new HavokRagdollBoneBinding(bone, body));

        return model;
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

    private static HavokRigidBody? Body(BehaviourGraphModel graph, int id, IReadOnlyDictionary<string, string> cinfo)
    {
        string position = cinfo.TryGetValue("position", out var p) ? p : "";
        string orientation = cinfo.TryGetValue("orientation", out var o) ? o : "";
        if (!Vector4(position, out var frame) || !Vector4(orientation, out var quat)) return null;

        int shapeId = cinfo.TryGetValue("shape", out var shape)
            ? RefId(shape)
            : -1;

        return new HavokRigidBody
        {
            Id = id,
            Name = cinfo.TryGetValue("name", out var name) ? name.Trim() : "",
            ShapeId = shapeId,
            Position = new Vector3(frame.X, frame.Y, frame.Z),
            Rotation = new Quaternion(quat.X, quat.Y, quat.Z, quat.W),
        };
    }

    private static HavokConstraint? Constraint(BehaviourGraphModel graph, int id, IReadOnlyDictionary<string, string> cinfo)
    {
        int bodyA = Int(cinfo, "bodyA");
        int bodyB = Int(cinfo, "bodyB");
        if (bodyA < 0 || bodyB < 0) return null;

        string kindName = "";
        if (cinfo.TryGetValue("constraintData", out var reference) && RefId(reference) is { } refId && refId >= 0)
            kindName = graph.Get(refId.ToString())?.Class ?? "";

        return new HavokConstraint
        {
            Id = id,
            BodyA = bodyA,
            BodyB = bodyB,
            Kind = KindOfConstraint(kindName),
        };
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