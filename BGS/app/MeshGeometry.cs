using System;
using System.Collections.Generic;
using System.Numerics;

namespace BehaviourStudio.App;

internal sealed class MeshPart
{
    public Vector3[] Positions = Array.Empty<Vector3>();
    public int[] Indices = Array.Empty<int>();
    public Vector3[]? Normals;
    public Vector2[]? Uvs;
}

internal sealed class MeshGeometry
{
    public const int FloatsPerVertex = 6;

    public Vector3[] Positions { get; private init; } = Array.Empty<Vector3>();
    public Vector3[] Normals { get; private init; } = Array.Empty<Vector3>();
    public Vector2[]? Uvs { get; private init; }
    public int[] Indices { get; private init; } = Array.Empty<int>();

    public int VertexCount => Positions.Length;
    public int TriangleCount => Indices.Length / 3;
    public int Version { get; set; }

    public static MeshGeometry FromParts(IReadOnlyList<MeshPart> parts)
    {
        int vertexCount = 0, indexCount = 0;
        bool anyUvs = parts.Count > 0;
        bool allNormals = parts.Count > 0;

        foreach (var part in parts)
        {
            vertexCount += part.Positions.Length;
            indexCount += part.Indices.Length;
            if (part.Uvs == null || part.Uvs.Length != part.Positions.Length) anyUvs = false;
            if (part.Normals == null || part.Normals.Length != part.Positions.Length) allNormals = false;
        }

        var positions = new Vector3[vertexCount];
        var indices = new int[indexCount];
        var normals = new Vector3[vertexCount];
        var uvs = anyUvs ? new Vector2[vertexCount] : null;

        int vertexAt = 0, indexAt = 0;
        var missing = new List<(int Start, int Count, int[] Indices)>();
        foreach (var part in parts)
        {
            Array.Copy(part.Positions, 0, positions, vertexAt, part.Positions.Length);
            for (int i = 0; i < part.Indices.Length; i++) indices[indexAt + i] = part.Indices[i] + vertexAt;
            if (uvs != null && part.Uvs != null) Array.Copy(part.Uvs, 0, uvs, vertexAt, part.Uvs.Length);

            if (part.Normals is { } supplied && supplied.Length == part.Positions.Length)
                Array.Copy(supplied, 0, normals, vertexAt, supplied.Length);
            else
                missing.Add((vertexAt, part.Positions.Length, part.Indices));

            vertexAt += part.Positions.Length;
            indexAt += part.Indices.Length;
        }

        if (!allNormals)
        {
            var computed = ComputeNormals(positions, indices);
            foreach (var (start, count, _) in missing)
                for (int i = 0; i < count; i++) normals[start + i] = computed[start + i];
        }

        return new MeshGeometry
        {
            Positions = positions,
            Normals = normals,
            Indices = indices,
            Uvs = uvs,
        };
    }

    public static Vector3[] ComputeNormals(Vector3[] positions, int[] indices)
    {
        var normals = new Vector3[positions.Length];
        for (int t = 0; t + 2 < indices.Length; t += 3)
        {
            int a = indices[t], b = indices[t + 1], c = indices[t + 2];
            if (a < 0 || b < 0 || c < 0) continue;
            if (a >= positions.Length || b >= positions.Length || c >= positions.Length) continue;
            if (a == b || b == c || a == c) continue;

            var face = Vector3.Cross(positions[b] - positions[a], positions[c] - positions[a]);
            normals[a] += face;
            normals[b] += face;
            normals[c] += face;
        }

        for (int i = 0; i < normals.Length; i++)
        {
            float length = normals[i].Length();
            normals[i] = length > 0.000001f ? normals[i] / length : new Vector3(0, 0, 1);
        }
        return normals;
    }

    public float[] Pack() => PackVertices(Positions, Normals);

    public static float[] PackVertices(Vector3[] positions, Vector3[] normals)
    {
        var packed = new float[positions.Length * FloatsPerVertex];
        for (int i = 0; i < positions.Length; i++)
        {
            int at = i * FloatsPerVertex;
            packed[at] = positions[i].X;
            packed[at + 1] = positions[i].Y;
            packed[at + 2] = positions[i].Z;

            var normal = i < normals.Length ? normals[i] : new Vector3(0, 0, 1);
            packed[at + 3] = normal.X;
            packed[at + 4] = normal.Y;
            packed[at + 5] = normal.Z;
        }
        return packed;
    }
}
