using System;
using System.Collections.Generic;
using System.Numerics;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public enum PlainCollisionShapeKind : uint
{
    Box = 1,
    Sphere = 2,
    Capsule = 3,
    ConvexHull = 4,
    TriangleMesh = 5,
}

public sealed class PlainCollisionShape
{
    public required uint CollisionIndex { get; init; }
    public required uint Body { get; init; }
    public required PlainCollisionShapeKind Kind { get; init; }
    public required uint Material { get; init; }
    public required Vector3 Origin { get; init; }
    public required float[] Basis { get; init; }
    public required Vector3 HalfExtents { get; init; }
    public required float Radius { get; init; }
    public required Vector3 PointA { get; init; }
    public required Vector3 PointB { get; init; }
    public required float Margin { get; init; }
    public required float[] Points { get; init; }
    public required uint[] Indices { get; init; }
    public required uint[] TriangleMaterials { get; init; }
}

// Invariant: engine axes and metres; Rotation is row-major column-vector like a body Basis; Scale is the node chain's uniform scale and is not folded into Rotation; the root node is not part of the chain.
public sealed record PlainCollisionTransform(Vector3 Translation, float[] Rotation, float Scale)
{
    public static PlainCollisionTransform Identity => new(new Vector3(0, 0, 0), [1, 0, 0, 0, 1, 0, 0, 0, 1], 1f);

    internal static unsafe PlainCollisionTransform FromBuffers(float* translation, float* rotation, float scale)
    {
        var matrix = new float[9];
        for (var i = 0; i < matrix.Length; ++i) matrix[i] = rotation[i];
        return new PlainCollisionTransform(new Vector3(translation[0], translation[1], translation[2]), matrix, scale);
    }
}

// Invariant: the first bhkNPCollisionObject naming the body's system and Body ID; TargetNodeIndex is -1 when its target is not a node.
public sealed record PlainCollisionOwner(int OwnerBlock, int TargetNodeIndex, PlainCollisionTransform Transform);

public sealed class PlainCollisionBody
{
    public required uint CollisionIndex { get; init; }
    public required uint Body { get; init; }
    public required uint Layer { get; init; }
    public required bool Dynamic { get; init; }
    public required byte TriggerType { get; init; }
    public required float? Mass { get; init; }
    public required Vector3? CenterOfMass { get; init; }
    public required Vector3 Origin { get; init; }
    public required float[] Basis { get; init; }
    public required uint SkippedShapes { get; init; }
    public required IReadOnlyList<PlainCollisionShape> Shapes { get; init; }

    // Invariant: null when no collision object names this body; the body is still listed.
    public PlainCollisionOwner? Owner { get; init; }
}

public sealed class PlainCollision
{
    private PlainCollision(IReadOnlyList<PlainCollisionBody> bodies, IReadOnlyList<PlainCollisionShape> shapes, uint skippedShapes, uint undecodableSystems)
    {
        Bodies = bodies;
        Shapes = shapes;
        SkippedShapes = skippedShapes;
        UndecodableSystems = undecodableSystems;
    }

    public IReadOnlyList<PlainCollisionBody> Bodies { get; }
    public IReadOnlyList<PlainCollisionShape> Shapes { get; }
    public uint SkippedShapes { get; }
    public uint UndecodableSystems { get; }

    public static unsafe PlainCollision FromBytes(ReadOnlySpan<byte> bytes)
    {
        if (bytes.IsEmpty) throw new ArgumentException("a NIF needs at least a header", nameof(bytes));
        IntPtr nif = IntPtr.Zero;
        fixed (byte* pointer = bytes)
        {
            Call.Check(RadicleNative.NifRead(pointer, (nuint)bytes.Length, &nif));
        }
        try
        {
            IntPtr plain = IntPtr.Zero;
            Call.Check(RadicleNative.NifCollisionPlain(nif, &plain));
            try
            {
                return Read(plain);
            }
            finally
            {
                RadicleNative.CollisionPlainDestroy(plain);
            }
        }
        finally
        {
            RadicleNative.NifDestroy(nif);
        }
    }

    private static unsafe PlainCollision Read(IntPtr plain)
    {
        nuint shapeCount = 0;
        Call.Check(RadicleNative.CollisionPlainShapeCount(plain, &shapeCount));
        var shapes = new PlainCollisionShape[checked((int)shapeCount)];
        for (var index = 0; index < shapes.Length; ++index) shapes[index] = ReadShape(plain, (nuint)index);

        nuint bodyCount = 0;
        Call.Check(RadicleNative.CollisionPlainBodyCount(plain, &bodyCount));
        var bodies = new PlainCollisionBody[checked((int)bodyCount)];
        for (var index = 0; index < bodies.Length; ++index) bodies[index] = ReadBody(plain, (nuint)index, shapes);

        uint skipped = 0;
        uint undecodable = 0;
        Call.Check(RadicleNative.CollisionPlainCounts(plain, &skipped, &undecodable));
        return new PlainCollision(bodies, shapes, skipped, undecodable);
    }

    private static unsafe PlainCollisionBody ReadBody(IntPtr plain, nuint index, PlainCollisionShape[] shapes)
    {
        CollisionPlainBody body;
        Call.Check(RadicleNative.CollisionPlainReadBody(plain, index, &body));
        var first = checked((int)body.FirstShape);
        var count = checked((int)body.ShapeCount);
        if (first < 0 || count < 0 || first + count > shapes.Length) throw new InvalidOperationException("a collision body names shapes outside the shape list");
        return new PlainCollisionBody {
            CollisionIndex = body.CollisionIndex,
            Body = body.Body,
            Layer = body.Layer,
            Dynamic = body.Dynamic != 0,
            TriggerType = body.TriggerType,
            Mass = body.HasMass != 0 ? body.Mass : null,
            CenterOfMass = body.HasCenterOfMass != 0 ? ToVector(body.CenterOfMass) : null,
            Origin = ToVector(body.Origin),
            Basis = ToMatrix(body.Basis),
            SkippedShapes = body.SkippedShapes,
            Shapes = new ArraySegment<PlainCollisionShape>(shapes, first, count),
            Owner = ReadOwner(plain, index),
        };
    }

    private static unsafe PlainCollisionOwner? ReadOwner(IntPtr plain, nuint index)
    {
        int ownerBlock = -1;
        int targetNode = -1;
        var translation = stackalloc float[3];
        var rotation = stackalloc float[9];
        float scale = 1f;
        Call.Check(RadicleNative.CollisionPlainBodyOwner(plain, index, &ownerBlock, &targetNode, translation, rotation, &scale));
        return ownerBlock < 0 ? null : new PlainCollisionOwner(ownerBlock, targetNode, PlainCollisionTransform.FromBuffers(translation, rotation, scale));
    }

    private static unsafe PlainCollisionShape ReadShape(IntPtr plain, nuint index)
    {
        CollisionPlainShape shape;
        Call.Check(RadicleNative.CollisionPlainReadShape(plain, index, &shape));
        uint* indices = null;
        nuint indexCount = 0;
        Call.Check(RadicleNative.CollisionPlainShapeIndices(plain, index, &indices, &indexCount));
        var indexValues = Call.TakeNodeRefs(indices, indexCount);
        uint* materials = null;
        nuint materialCount = 0;
        Call.Check(RadicleNative.CollisionPlainShapeTriangleMaterials(plain, index, &materials, &materialCount));
        var materialValues = Call.TakeNodeRefs(materials, materialCount);
        return new PlainCollisionShape {
            CollisionIndex = shape.CollisionIndex,
            Body = shape.Body,
            Kind = (PlainCollisionShapeKind)shape.Kind,
            Material = shape.Material,
            Origin = ToVector(shape.Origin),
            Basis = ToMatrix(shape.Basis),
            HalfExtents = ToVector(shape.HalfExtents),
            Radius = shape.Radius,
            PointA = ToVector(shape.PointA),
            PointB = ToVector(shape.PointB),
            Margin = shape.Margin,
            Points = Call.ReadFloats(plain, index, RadicleNative.CollisionPlainShapePoints),
            Indices = indexValues,
            TriangleMaterials = materialValues,
        };
    }

    private static unsafe Vector3 ToVector(float* values) => new(values[0], values[1], values[2]);

    private static unsafe float[] ToMatrix(float* values)
    {
        var result = new float[9];
        for (var i = 0; i < result.Length; ++i) result[i] = values[i];
        return result;
    }
}

public sealed class PlainCollisionNames : IDisposable
{
    private IntPtr _handle;
    private bool _disposed;

    private PlainCollisionNames(IntPtr handle) { _handle = handle; }

    public static unsafe PlainCollisionNames Build(RadicleEnvironment environment)
    {
        ArgumentNullException.ThrowIfNull(environment);
        IntPtr handle = IntPtr.Zero;
        using (var use = environment.Use())
        {
            Call.Check(RadicleNative.CollisionNamesBuild(use.Pointer, &handle));
        }
        return new PlainCollisionNames(handle);
    }

    public void Dispose()
    {
        if (!_disposed) { RadicleNative.CollisionNamesDestroy(_handle); _disposed = true; }
        GC.SuppressFinalize(this);
    }

    ~PlainCollisionNames() { if (!_disposed) RadicleNative.CollisionNamesDestroy(_handle); }

    public unsafe string? Layer(uint layer)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        IntPtr name = IntPtr.Zero;
        Call.Check(RadicleNative.CollisionNamesLayer(_handle, layer, &name));
        return Utf8.Read(name);
    }

    // Invariant: data-defined from the winning COLL CollidesWith lists: true only for a layer whose list is empty and that no other layer lists; false for an unnamed layer or one past LayerCount.
    public unsafe bool CollidesWithNothing(uint layer)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        int flag = 0;
        Call.Check(RadicleNative.CollisionNamesLayerCollidesWithNothing(_handle, layer, &flag));
        return flag != 0;
    }

    // Invariant: the layer indexes its winning COLL CollidesWith list names, ascending and unique, copied out of the table; empty for an unnamed layer, a gap, one past LayerCount or a layer with no list.
    public unsafe uint[] CollidesWith(uint layer)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        uint* values = null;
        nuint count = 0;
        Call.Check(RadicleNative.CollisionNamesLayerCollidesWith(_handle, layer, &values, &count));
        return Call.Copy<uint>((IntPtr)values, count);
    }

    public unsafe string? Material(uint material)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        IntPtr name = IntPtr.Zero;
        Call.Check(RadicleNative.CollisionNamesMaterial(_handle, material, &name));
        return Utf8.Read(name);
    }

    public int LayerCount
    {
        get
        {
            Counts(out var layers, out _);
            return layers;
        }
    }

    public int MaterialCount
    {
        get
        {
            Counts(out _, out var materials);
            return materials;
        }
    }

    private unsafe void Counts(out int layers, out int materials)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        nuint layerCount = 0;
        nuint materialCount = 0;
        Call.Check(RadicleNative.CollisionNamesCounts(_handle, &layerCount, &materialCount));
        layers = checked((int)layerCount);
        materials = checked((int)materialCount);
    }
}
