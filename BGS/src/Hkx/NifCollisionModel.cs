using System;
using System.Collections.Generic;
using System.Security.Cryptography;
using System.Text;

namespace OpenCommonwealth.Services.Hkx;

public enum NifCollisionShapeKind
{
    Unknown,
    Sphere,
    ConvexPolytope,
    Compound,
    CompressedMesh,
}

// Invariant: response floats are the half-precision values widened, never reinterpreted.
public sealed class NifCollisionResponse
{
    public float DynamicFriction { get; init; }
    public float StaticFriction { get; init; }
    public float Restitution { get; init; }
    public byte FrictionCombine { get; init; }
    public byte RestitutionCombine { get; init; }
}

public sealed class NifCollisionBody
{
    public int Index { get; init; }
    public int Shape { get; init; } = -1;
    public ushort MaterialId { get; init; }
    public uint MaterialHash { get; init; }
    public int Flags { get; init; }
    public float[] Position { get; init; } = new float[4];
    public float[] Orientation { get; init; } = new float[4];
    public NifCollisionResponse Response { get; init; } = new();
}

public sealed class NifCollisionInstance
{
    public int Shape { get; init; } = -1;
    public float[] Transform { get; init; } = new float[16];
    public float[] Scale { get; init; } = new float[4];
}

public sealed class NifCollisionShape
{
    public int Index { get; init; }
    public string Class { get; init; } = "";
    public NifCollisionShapeKind Kind { get; init; }
    public float ConvexRadius { get; init; }
    public List<float> Vertices { get; } = new();
    public List<int> Triangles { get; } = new();
    public List<NifCollisionInstance> Instances { get; } = new();

    public int VertexCount => Vertices.Count / 3;
}

// Constraint: these are the NIF collision object's facts, handed in by the reader that found the system; a blob states none of them.
public sealed class NifCollisionAttribution
{
    public int OwnerBlock { get; init; } = -1;
    public ushort Flags { get; init; }
    public int SystemRef { get; init; } = -1;
    public uint BodyId { get; init; }
    public int TargetBlock { get; init; } = -1;
    public int TargetNode { get; init; } = -1;
    public string TargetNodeName { get; init; } = "";
}

public sealed class NifCollisionModel
{
    public int PointerSize { get; init; }
    public bool LengthPrefixed { get; init; }
    public string SystemClass { get; init; } = "";
    // Constraint: Describe and Sha256Hex cover the payload only, so two NIFs naming one system keep one hash.
    public NifCollisionAttribution? Attribution { get; init; }
    public List<NifCollisionBody> Bodies { get; } = new();
    public List<NifCollisionShape> Shapes { get; } = new();
    public List<string> Notes { get; } = new();

    // Constraint: one record per line, floats through HkxNumber, so a manifest compares across runs.
    public string Describe()
    {
        var text = new StringBuilder();
        text.Append("layout ").Append(PointerSize).Append(' ')
            .Append(LengthPrefixed ? "length-prefixed" : "bare").Append(' ')
            .Append(SystemClass).Append('\n');

        text.Append("bodies ").Append(Bodies.Count).Append('\n');
        foreach (var body in Bodies)
        {
            text.Append("b ").Append(body.Index)
                .Append(' ').Append(body.Shape)
                .Append(' ').Append(body.MaterialId)
                .Append(' ').Append(body.MaterialHash.ToString("x8"))
                .Append(' ').Append(body.Flags)
                .Append(' ').Append(Floats(body.Position))
                .Append(' ').Append(Floats(body.Orientation))
                .Append(' ').Append(Floats(body.Response))
                .Append('\n');
        }

        text.Append("shapes ").Append(Shapes.Count).Append('\n');
        foreach (var shape in Shapes)
        {
            text.Append("s ").Append(shape.Index)
                .Append(' ').Append(shape.Class)
                .Append(' ').Append(shape.Kind)
                .Append(' ').Append(HkxNumber.Text(shape.ConvexRadius))
                .Append(' ').Append(shape.VertexCount)
                .Append(' ').Append(shape.Triangles.Count / 3)
                .Append(' ').Append(shape.Instances.Count)
                .Append('\n');

            for (int at = 0; at + 2 < shape.Vertices.Count; at += 3)
                text.Append("v ").Append(shape.Index).Append(' ').Append(at / 3).Append(' ')
                    .Append(HkxNumber.Text(shape.Vertices[at])).Append(' ')
                    .Append(HkxNumber.Text(shape.Vertices[at + 1])).Append(' ')
                    .Append(HkxNumber.Text(shape.Vertices[at + 2])).Append('\n');

            for (int at = 0; at + 2 < shape.Triangles.Count; at += 3)
                text.Append("t ").Append(shape.Index).Append(' ').Append(at / 3).Append(' ')
                    .Append(shape.Triangles[at]).Append(' ').Append(shape.Triangles[at + 1]).Append(' ')
                    .Append(shape.Triangles[at + 2]).Append('\n');

            foreach (var instance in shape.Instances)
                text.Append("i ").Append(shape.Index).Append(' ').Append(instance.Shape).Append(' ')
                    .Append(Floats(instance.Transform)).Append(' ').Append(Floats(instance.Scale)).Append('\n');
        }

        foreach (var note in Notes) text.Append("note ").Append(note).Append('\n');

        return text.ToString();
    }

    public string Sha256Hex()
    {
        var digest = SHA256.HashData(Encoding.UTF8.GetBytes(Describe()));
        return Convert.ToHexString(digest);
    }

    private static string Floats(float[] values)
    {
        var text = new StringBuilder();
        foreach (var value in values)
        {
            if (text.Length > 0) text.Append(' ');
            text.Append(HkxNumber.Text(value));
        }
        return text.ToString();
    }

    private static string Floats(NifCollisionResponse response) =>
        HkxNumber.Text(response.DynamicFriction) + " " +
        HkxNumber.Text(response.StaticFriction) + " " +
        HkxNumber.Text(response.Restitution) + " " +
        response.FrictionCombine + " " + response.RestitutionCombine;
}
