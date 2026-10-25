using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Linq;

namespace OpenCommonwealth.Services.Hkx;

// Constraint: every member resolves by name through HavokClassTypes + LayoutWalker at the file's own pointer width, no pointer is trusted past the data section, and the table's fit for these blobs stays unverified until the corpus run.
public static class NifCollisionDecoder
{
    public const uint Magic0 = 0x57E0E057;
    public const uint Magic1 = 0x10C0C010;

    public const string SystemClass = "hknpPhysicsSystemData";
    public const string RagdollClass = "hknpRagdollData";

    private const int MaxBodies = 4096;
    private const int MaxShapes = 8192;
    private const int MaxDepth = 8;
    private const int MaxSections = 1024;
    private const int MaxVertices = 1 << 20;
    private const int MaxTriangles = 1 << 20;
    private const int CustomPrimitive = 3;
    private const int ExternalPrimitive = 4;

    public static bool TryDecode(ReadOnlySpan<byte> blob, out NifCollisionModel? model, out string error) =>
        TryDecode(blob, null, out model, out error);

    // Constraint: attribution is passed in, never read from the blob, so a system no object names decodes without one.
    public static bool TryDecode(ReadOnlySpan<byte> blob, NifCollisionAttribution? attribution,
                                 out NifCollisionModel? model, out string error)
    {
        model = null;
        error = "";

        try
        {
            model = Decode(blob, attribution, out error);
            return model != null;
        }
        catch (Exception failure)
        {
            error = failure.GetType().Name + ": " + failure.Message;
            model = null;
            return false;
        }
    }

    private static NifCollisionModel? Decode(ReadOnlySpan<byte> blob, NifCollisionAttribution? attribution,
                                             out string error)
    {
        error = "";

        if (blob.Length < PackfileImage.HeaderSize)
        {
            error = "blob is shorter than a packfile header";
            return null;
        }

        bool prefixed = false;
        if (BinaryPrimitives.ReadUInt32LittleEndian(blob) != Magic0)
        {
            if (blob.Length < PackfileImage.HeaderSize + 4 ||
                BinaryPrimitives.ReadUInt32LittleEndian(blob[4..]) != Magic0)
            {
                error = "blob does not start with the Havok packfile magic";
                return null;
            }
            prefixed = true;
        }

        var bytes = (prefixed ? blob[4..] : blob).ToArray();
        var image = PackfileImage.Read(bytes);
        var objects = new PackfileObjects(image);

        var system = objects.Instances.FirstOrDefault(instance =>
            instance.ClassName is SystemClass or RagdollClass);
        if (system == null)
        {
            error = "blob has no hknpPhysicsSystemData or hknpRagdollData object";
            return null;
        }

        var model = new NifCollisionModel
        {
            PointerSize = image.Layout.PointerSize,
            LengthPrefixed = prefixed,
            SystemClass = system.ClassName,
            Attribution = attribution,
        };

        // Constraint: a class whose blob signature disagrees with the class table is refused, never decoded at the table's offsets.
        foreach (var (signature, name) in objects.ClassNames())
        {
            if (name != system.ClassName && name != "hknpBodyCinfo") continue;
            if (HavokClassTypes.Shipped[name] is not { } declared || declared.Signature == signature) continue;

            error = $"{name} is signed 0x{signature:x8} in the blob and 0x{declared.Signature:x8} in the class table";
            return null;
        }

        var materials = Materials(objects, system, model);
        var hashes = MaterialHashes(objects);
        var shapes = new ShapeReader(objects, model);

        var bodyLayout = LayoutWalker.Active(HavokClassTypes.Shipped, "hknpBodyCinfo", objects.PointerWidth);
        if (bodyLayout == null)
        {
            error = "hknpBodyCinfo cannot be laid out at this pointer width";
            return null;
        }

        if (At(objects, system, "bodyCinfos") is { } header &&
            objects.ArrayAt(header, bodyLayout.Size) is { } bodyArray)
        {
            for (int i = 0; i < bodyArray.Count; i++)
            {
                if (model.Bodies.Count >= MaxBodies)
                {
                    model.Notes.Add($"bodies capped at {MaxBodies}");
                    break;
                }

                int at = bodyArray.At + i * bodyLayout.Size;
                var body = Body(objects, bodyLayout, at, i, shapes, materials, hashes);
                if (body != null) model.Bodies.Add(body);
            }
        }

        return model;
    }

    private static NifCollisionBody? Body(PackfileObjects objects, ObjectLayout layout, int at, int index,
                                         ShapeReader shapes, IReadOnlyList<NifCollisionResponse> materials,
                                         IReadOnlyList<uint> hashes)
    {
        var position = layout.OffsetOf("position") is int p ? objects.ReadFloatsAt(at + p, 4) : null;
        var orientation = layout.OffsetOf("orientation") is int q ? objects.ReadFloatsAt(at + q, 4) : null;
        var materialId = layout.OffsetOf("materialId") is int m ? objects.ReadNarrowAt(at + m, 2) : null;
        var flags = layout.OffsetOf("flags") is int f ? objects.ReadIntAt(at + f) : null;
        var shapeOffset = layout.OffsetOf("shape");

        var target = shapeOffset is int s
            ? objects.ReadRefAt(at + s, out _)
            : null;

        bool hasMaterial = materialId is int known && known >= 0 && known <= ushort.MaxValue;
        ushort id = hasMaterial ? (ushort)materialId.GetValueOrDefault() : (ushort)0;

        return new NifCollisionBody
        {
            Index = index,
            Shape = target == null ? -1 : shapes.IndexOf(target),
            MaterialId = id,
            MaterialHash = hasMaterial && id < hashes.Count ? hashes[id] : 0u,
            Flags = flags ?? 0,
            Position = position ?? new float[4],
            Orientation = orientation ?? new float[4],
            Response = hasMaterial && id < materials.Count ? materials[id] : new NifCollisionResponse(),
        };
    }

    private static IReadOnlyList<NifCollisionResponse> Materials(PackfileObjects objects,
                                                                PackfileObjects.Instance system,
                                                                NifCollisionModel model)
    {
        var responses = new List<NifCollisionResponse>();
        var layout = LayoutWalker.Active(HavokClassTypes.Shipped, "hknpMaterial", objects.PointerWidth);
        if (layout == null || At(objects, system, "materials") is not { } header) return responses;
        if (objects.ArrayAt(header, layout.Size) is not { } array) return responses;

        for (int i = 0; i < array.Count && i < MaxBodies; i++)
        {
            int at = array.At + i * layout.Size;
            responses.Add(new NifCollisionResponse
            {
                DynamicFriction = Half(objects, layout, at, "dynamicFriction"),
                StaticFriction = Half(objects, layout, at, "staticFriction"),
                Restitution = Half(objects, layout, at, "restitution"),
                FrictionCombine = Byte(objects, layout, at, "frictionCombinePolicy"),
                RestitutionCombine = Byte(objects, layout, at, "restitutionCombinePolicy"),
            });
        }

        if (array.Count > MaxBodies) model.Notes.Add($"materials capped at {MaxBodies}");
        return responses;
    }

    private static IReadOnlyList<uint> MaterialHashes(PackfileObjects objects)
    {
        var hashes = new List<uint>();
        var layout = LayoutWalker.Active(HavokClassTypes.Shipped, "hknpBSMaterial", objects.PointerWidth);
        if (layout == null || layout.OffsetOf("uiMaterialCRC") is not int crcOffset) return hashes;

        int owned = 0;
        foreach (var properties in objects.Instances.Where(i => i.ClassName == "hknpBSMaterialProperties"))
        {
            if (At(objects, properties, "MaterialA") is not { } header) continue;
            if (objects.ArrayAt(header, layout.Size) is not { } array) continue;

            for (int i = 0; i < array.Count && owned < MaxBodies; i++, owned++)
                hashes.Add((uint)(objects.ReadUnsignedAt(array.At + i * layout.Size + crcOffset, 4) ?? 0));
        }

        return hashes;
    }

    private static float Half(PackfileObjects objects, ObjectLayout layout, int at, string member) =>
        layout.OffsetOf(member) is int offset && objects.ReadUnsignedAt(at + offset, 2) is { } bits
            ? (float)BitConverter.UInt16BitsToHalf((ushort)bits)
            : 0.0f;

    private static byte Byte(PackfileObjects objects, ObjectLayout layout, int at, string member) =>
        layout.OffsetOf(member) is int offset && objects.ReadUnsignedAt(at + offset, 1) is { } value
            ? (byte)value
            : (byte)0;

    private static int? At(PackfileObjects objects, PackfileObjects.Instance instance, string member)
    {
        var layout = LayoutWalker.Active(HavokClassTypes.Shipped, instance.ClassName, objects.PointerWidth);
        return layout?.OffsetOf(member) is int offset ? instance.Offset + offset : null;
    }

    private sealed class ShapeReader
    {
        private readonly PackfileObjects _objects;
        private readonly NifCollisionModel _model;
        private readonly Dictionary<int, int> _byOffset = new();

        public ShapeReader(PackfileObjects objects, NifCollisionModel model)
        {
            _objects = objects;
            _model = model;
        }

        public int IndexOf(PackfileObjects.Instance? instance, int depth = 0)
        {
            if (instance == null) return -1;
            if (_byOffset.TryGetValue(instance.Offset, out int known)) return known;
            if (_model.Shapes.Count >= MaxShapes)
            {
                NoteOnce($"shapes capped at {MaxShapes}");
                return -1;
            }
            if (depth >= MaxDepth)
            {
                NoteOnce($"shape nesting capped at {MaxDepth}");
                return -1;
            }

            var kind = KindOf(instance.ClassName);
            var layout = LayoutWalker.Active(HavokClassTypes.Shipped, instance.ClassName, _objects.PointerWidth);
            var shape = new NifCollisionShape
            {
                Index = _model.Shapes.Count,
                Class = instance.ClassName,
                Kind = kind,
                ConvexRadius = layout?.OffsetOf("convexRadius") is int radius
                    ? _objects.ReadFloatAt(instance.Offset + radius) ?? 0.0f
                    : 0.0f,
            };
            _model.Shapes.Add(shape);
            _byOffset[instance.Offset] = shape.Index;

            if (layout == null)
            {
                NoteOnce($"{instance.ClassName} cannot be laid out at this pointer width");
                return shape.Index;
            }

            switch (kind)
            {
                case NifCollisionShapeKind.ConvexPolytope:
                    Polytope(shape, instance, layout);
                    break;
                case NifCollisionShapeKind.Compound:
                    Compound(shape, instance, layout, depth);
                    break;
                case NifCollisionShapeKind.CompressedMesh:
                    CompressedMesh(shape, instance, layout);
                    break;
            }

            return shape.Index;
        }

        private void Polytope(NifCollisionShape shape, PackfileObjects.Instance instance, ObjectLayout layout)
        {
            if (layout.OffsetOf("vertices") is not int verticesAt) return;
            if (_objects.RelArrayAt(instance.Offset + verticesAt, 16) is not { } vertices) return;

            for (int i = 0; i < vertices.Count; i++)
            {
                if (shape.VertexCount >= MaxVertices) { NoteOnce($"vertices capped at {MaxVertices}"); break; }
                var row = _objects.ReadFloatsAt(vertices.At + i * 16, 4);
                if (row == null) break;
                shape.Vertices.Add(row[0]);
                shape.Vertices.Add(row[1]);
                shape.Vertices.Add(row[2]);
            }

            if (layout.OffsetOf("faces") is not int facesAt || layout.OffsetOf("indices") is not int indicesAt) return;
            if (_objects.RelArrayAt(instance.Offset + indicesAt, 1) is not { } indices) return;

            var faceClass = HavokClassTypes.Shipped.Members(instance.ClassName)
                .FirstOrDefault(member => member.Name == "faces")?.CType;
            if (faceClass == null) return;

            var faceLayout = LayoutWalker.Active(HavokClassTypes.Shipped, faceClass, _objects.PointerWidth);
            if (faceLayout == null || faceLayout.Size <= 0) return;
            if (faceLayout.OffsetOf("firstIndex") is not int firstIndexAt) return;
            if (faceLayout.OffsetOf("numIndices") is not int numIndicesAt) return;
            if (_objects.RelArrayAt(instance.Offset + facesAt, faceLayout.Size) is not { } faces) return;

            for (int i = 0; i < faces.Count; i++)
            {
                int at = faces.At + i * faceLayout.Size;
                int first = _objects.ReadNarrowAt(at + firstIndexAt, 2) ?? 0;
                int count = _objects.ReadNarrowAt(at + numIndicesAt, 1) ?? 0;
                if (count < 3 || first < 0 || first + count > indices.Count) continue;

                for (int corner = 1; corner + 1 < count; corner++)
                {
                    if (shape.Triangles.Count / 3 >= MaxTriangles) { NoteOnce($"triangles capped at {MaxTriangles}"); return; }
                    AddTriangle(shape, first, 0, corner, corner + 1, indices);
                }
            }
        }

        private void AddTriangle(NifCollisionShape shape, int index, int a, int b, int c,
                                 PackfileObjects.RelElements indices)
        {
            int ia = _objects.ReadNarrowAt(indices.At + index + a, 1) ?? -1;
            int ib = _objects.ReadNarrowAt(indices.At + index + b, 1) ?? -1;
            int ic = _objects.ReadNarrowAt(indices.At + index + c, 1) ?? -1;
            if (ia < 0 || ib < 0 || ic < 0) return;
            if (ia >= shape.VertexCount || ib >= shape.VertexCount || ic >= shape.VertexCount) return;

            shape.Triangles.Add(ia);
            shape.Triangles.Add(ib);
            shape.Triangles.Add(ic);
        }

        private void Compound(NifCollisionShape shape, PackfileObjects.Instance instance, ObjectLayout layout, int depth)
        {
            if (layout.OffsetOf("instances") is not int instancesAt) return;

            var instanceLayout = LayoutWalker.Active(HavokClassTypes.Shipped, "hknpShapeInstance",
                                                    _objects.PointerWidth);
            if (instanceLayout == null || instanceLayout.Size <= 0) return;
            if (instanceLayout.OffsetOf("shape") is not int shapeAt) return;
            if (_objects.ArrayAt(instance.Offset + instancesAt, instanceLayout.Size) is not { } array) return;

            for (int i = 0; i < array.Count && i < MaxShapes; i++)
            {
                int at = array.At + i * instanceLayout.Size;
                var child = _objects.ReadRefAt(at + shapeAt, out _);
                if (child == null)
                {
                    NoteOnce("compound free-list holes skipped");
                    continue;
                }

                var transform = _objects.ReadFloatsAt(at + (instanceLayout.OffsetOf("transform") ?? 0), 16);
                var scale = instanceLayout.OffsetOf("scale") is int sc ? _objects.ReadFloatsAt(at + sc, 4) : null;

                shape.Instances.Add(new NifCollisionInstance
                {
                    Shape = IndexOf(child, depth + 1),
                    Transform = transform ?? new float[16],
                    Scale = scale ?? new float[4],
                });
            }
        }

        private void CompressedMesh(NifCollisionShape shape, PackfileObjects.Instance instance, ObjectLayout layout)
        {
            if (layout.OffsetOf("data") is not int dataAt) return;
            var data = _objects.ReadRefAt(instance.Offset + dataAt, out _);
            if (data == null) return;

            var dataLayout = LayoutWalker.Active(HavokClassTypes.Shipped, data.ClassName, _objects.PointerWidth);
            if (dataLayout?.OffsetOf("meshTree") is not int treeAt) return;
            int tree = data.Offset + treeAt;

            var treeLayout = LayoutWalker.Active(HavokClassTypes.Shipped, "hknpCompressedMeshShapeTree",
                                                _objects.PointerWidth);
            var sectionLayout = LayoutWalker.Active(HavokClassTypes.Shipped, "hkcdStaticMeshTreeBaseSection",
                                                   _objects.PointerWidth);
            if (treeLayout == null || sectionLayout == null) return;
            if (sectionLayout.OffsetOf("primitives") is not int firstPrimitiveAt) return;

            var domain = treeLayout.OffsetOf("domain") is int d ? _objects.ReadFloatsAt(tree + d, 8) : null;
            var sections = FieldArray(tree, treeLayout, "sections", sectionLayout.Size);
            var primitives = FieldArray(tree, treeLayout, "primitives", 4);
            var sharedIndex = FieldArray(tree, treeLayout, "sharedVerticesIndex", 2);
            var packed = FieldArray(tree, treeLayout, "packedVertices", 4);
            var shared = FieldArray(tree, treeLayout, "sharedVertices", 8);
            if (sections == null || primitives == null) return;

            for (int i = 0; i < sections.Count && i < MaxSections; i++)
            {
                int at = sections.At + i * sectionLayout.Size;
                int next = i + 1 < sections.Count
                    ? _objects.ReadIntAt(sections.At + (i + 1) * sectionLayout.Size + firstPrimitiveAt) ?? -1
                    : primitives.Count;
                if (!Section(shape, at, sectionLayout, domain, primitives, sharedIndex, packed, shared, i, next))
                    break;
            }

            if (sections.Count > MaxSections) NoteOnce($"sections capped at {MaxSections}");
        }

        private bool Section(NifCollisionShape shape, int at, ObjectLayout layout, float[]? domain,
                             PackfileObjects.Elements? primitives, PackfileObjects.Elements? sharedIndex,
                             PackfileObjects.Elements? packed, PackfileObjects.Elements? shared, int index,
                             int nextPrimitive)
        {
            if (primitives == null) return true;
            if (layout.OffsetOf("codecParms") is not int p ||
                layout.OffsetOf("numPackedVertices") is not int packedCountAt ||
                layout.OffsetOf("numSharedIndices") is not int sharedCountAt ||
                layout.OffsetOf("firstPackedVertex") is not int firstPackedAt ||
                layout.OffsetOf("sharedVertices") is not int firstSharedAt ||
                layout.OffsetOf("primitives") is not int firstPrimitiveAt) return true;

            var parms = _objects.ReadFloatsAt(at + p, 6);
            int packedCount = _objects.ReadNarrowAt(at + packedCountAt, 1) ?? 0;
            int sharedCount = _objects.ReadNarrowAt(at + sharedCountAt, 1) ?? 0;
            int firstPacked = _objects.ReadIntAt(at + firstPackedAt) ?? -1;
            int firstShared = _objects.ReadIntAt(at + firstSharedAt) ?? -1;
            int firstPrimitive = _objects.ReadIntAt(at + firstPrimitiveAt) ?? -1;
            int page = layout.OffsetOf("page") is int pageAt ? _objects.ReadNarrowAt(at + pageAt, 1) ?? 0 : 0;

            int baseVertex = shape.VertexCount;
            if (packed == null || parms == null || firstPacked < 0 || firstShared < 0) return true;

            for (int i = 0; i < packedCount; i++)
            {
                if (shape.VertexCount >= MaxVertices) { NoteOnce($"vertices capped at {MaxVertices}"); return false; }
                int slot = packed.At + (firstPacked + i) * 4;
                if (_objects.ReadUnsignedAt(slot, 4) is not { } bits) return false;
                shape.Vertices.AddRange(Decode((uint)bits, 11, 11, 10, parms[0], parms[1], parms[2],
                                               parms[3], parms[4], parms[5]));
            }

            for (int i = 0; i < sharedCount; i++)
            {
                if (shape.VertexCount >= MaxVertices) { NoteOnce($"vertices capped at {MaxVertices}"); return false; }
                if (sharedIndex == null || shared == null) break;

                int slot = sharedIndex.At + (firstShared + i) * 2;
                if (_objects.ReadNarrowAt(slot, 2) is not { } sharedIdx) break;
                int vertex = shared.At + (page * 65536 + sharedIdx) * 8;
                if (_objects.ReadUnsignedAt(vertex, 8) is not { } bits) break;
                shape.Vertices.AddRange(Decode((ulong)bits, 21, 21, 22,
                                               domain?[0] ?? 0.0f, domain?[1] ?? 0.0f, domain?[2] ?? 0.0f,
                                               (domain?[4] ?? 0.0f) / 2097151.0f,
                                               (domain?[5] ?? 0.0f) / 2097151.0f,
                                               (domain?[6] ?? 0.0f) / 4194303.0f));
            }

            // Constraint: the primitive range is used only when it validates against the primitive array.
            int primitiveCount = nextPrimitive - firstPrimitive;
            if (firstPrimitive < 0 || primitiveCount <= 0 || nextPrimitive > primitives.Count)
            {
                NoteOnce($"section {index} primitive range not decoded");
                return true;
            }

            for (int i = 0; i < primitiveCount; i++)
            {
                int entry = primitives.At + (firstPrimitive + i) * 4;
                int a = _objects.ReadNarrowAt(entry, 1) ?? -1;
                int b = _objects.ReadNarrowAt(entry + 1, 1) ?? -1;
                int c = _objects.ReadNarrowAt(entry + 2, 1) ?? -1;
                int d = _objects.ReadNarrowAt(entry + 3, 1) ?? -1;
                if (a < 0 || b < 0 || c < 0 || d < 0) break;
                if (b == d)
                {
                    if (c == CustomPrimitive || c == ExternalPrimitive)
                        NoteOnce("custom or external mesh primitives skipped");
                    continue;
                }
                if (shape.Triangles.Count / 3 >= MaxTriangles) { NoteOnce($"triangles capped at {MaxTriangles}"); return false; }

                Triangle(shape, a, b, c, baseVertex);
                if (c != d) Triangle(shape, a, c, d, baseVertex);
            }

            return true;
        }

        private static void Triangle(NifCollisionShape shape, int a, int b, int c, int baseVertex)
        {
            int count = shape.VertexCount;
            if (baseVertex + a >= count || baseVertex + b >= count || baseVertex + c >= count) return;

            shape.Triangles.Add(baseVertex + a);
            shape.Triangles.Add(baseVertex + b);
            shape.Triangles.Add(baseVertex + c);
        }

        private static float[] Decode(ulong packed, int bitsX, int bitsY, int bitsZ,
                                      float originX, float originY, float originZ,
                                      float scaleX, float scaleY, float scaleZ)
        {
            double maskX = (1u << bitsX) - 1u;
            double maskY = (1u << bitsY) - 1u;
            double maskZ = (1UL << bitsZ) - 1UL;

            double x = packed & (ulong)maskX;
            double y = (packed >> bitsX) & (ulong)maskY;
            double z = (packed >> (bitsX + bitsY)) & (ulong)maskZ;

            return new[]
            {
                (float)(originX + x * scaleX),
                (float)(originY + y * scaleY),
                (float)(originZ + z * scaleZ),
            };
        }

        private PackfileObjects.Elements? FieldArray(int tree, ObjectLayout layout, string member, int stride) =>
            layout.OffsetOf(member) is int offset ? _objects.ArrayAt(tree + offset, stride) : null;

        private void NoteOnce(string note)
        {
            if (!_model.Notes.Contains(note)) _model.Notes.Add(note);
        }
    }

    private static NifCollisionShapeKind KindOf(string className) => className switch
    {
        "hknpSphereShape" => NifCollisionShapeKind.Sphere,
        "hknpConvexPolytopeShape" => NifCollisionShapeKind.ConvexPolytope,
        "hknpCompoundShape" or "hknpDynamicCompoundShape" => NifCollisionShapeKind.Compound,
        "hknpCompressedMeshShape" => NifCollisionShapeKind.CompressedMesh,
        _ => NifCollisionShapeKind.Unknown,
    };
}
