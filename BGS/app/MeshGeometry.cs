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

    private Vector3[] _positions = Array.Empty<Vector3>();
    private Vector3[] _normals = Array.Empty<Vector3>();
    private Vector2[]? _uvs;
    private int[] _indices = Array.Empty<int>();
    private Vector3[] _normalScratch = Array.Empty<Vector3>();
    private readonly object _sync = new();

    public Vector3[] Positions => _positions;
    public Vector3[] Normals => _normals;
    public Vector2[]? Uvs => _uvs;
    public int[] Indices => _indices;

    public int VertexCount => _positions.Length;
    public int TriangleCount => _indices.Length / 3;
    public int Version { get; set; }

    public static MeshGeometry FromParts(IReadOnlyList<MeshPart> parts)
    {
        var geometry = new MeshGeometry();
        geometry.Refresh(parts);
        return geometry;
    }

    public void Refresh(IReadOnlyList<MeshPart> parts)
    {
        lock (_sync) RefreshLocked(parts);
    }

    public int[] PackSnapshot(ref float[] packed)
    {
        lock (_sync)
        {
            int floats = _positions.Length * FloatsPerVertex;
            if (packed.Length != floats) packed = new float[floats];
            PackVertices(_positions, _normals, packed);
            return _indices;
        }
    }

    private void RefreshLocked(IReadOnlyList<MeshPart> parts)
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

        if (_positions.Length != vertexCount)
        {
            _positions = new Vector3[vertexCount];
            _normals = new Vector3[vertexCount];
            _normalScratch = new Vector3[vertexCount];
        }

        bool sameIndices = SameIndices(parts, _indices, indexCount);
        if (!sameIndices) _indices = new int[indexCount];
        if (!anyUvs) _uvs = null;
        else if (_uvs == null || _uvs.Length != vertexCount) _uvs = new Vector2[vertexCount];

        int vertexAt = 0, indexAt = 0;
        foreach (var part in parts)
        {
            Array.Copy(part.Positions, 0, _positions, vertexAt, part.Positions.Length);
            if (!sameIndices)
                for (int i = 0; i < part.Indices.Length; i++) _indices[indexAt + i] = part.Indices[i] + vertexAt;
            if (_uvs != null && part.Uvs != null) Array.Copy(part.Uvs, 0, _uvs, vertexAt, part.Uvs.Length);

            if (part.Normals is { } supplied && supplied.Length == part.Positions.Length)
                Array.Copy(supplied, 0, _normals, vertexAt, supplied.Length);

            vertexAt += part.Positions.Length;
            indexAt += part.Indices.Length;
        }

        if (!allNormals)
        {
            ComputeNormals(_positions, _indices, _normalScratch);
            int at = 0;
            foreach (var part in parts)
            {
                if (part.Normals == null || part.Normals.Length != part.Positions.Length)
                    Array.Copy(_normalScratch, at, _normals, at, part.Positions.Length);
                at += part.Positions.Length;
            }
        }
    }

    private static bool SameIndices(IReadOnlyList<MeshPart> parts, int[] current, int indexCount)
    {
        if (current.Length != indexCount) return false;
        int vertexAt = 0, indexAt = 0;
        foreach (var part in parts)
        {
            for (int i = 0; i < part.Indices.Length; i++)
                if (current[indexAt + i] != part.Indices[i] + vertexAt) return false;
            vertexAt += part.Positions.Length;
            indexAt += part.Indices.Length;
        }
        return true;
    }

    public static Vector3[] ComputeNormals(Vector3[] positions, int[] indices)
    {
        var normals = new Vector3[positions.Length];
        ComputeNormals(positions, indices, normals);
        return normals;
    }

    public static void ComputeNormals(Vector3[] positions, int[] indices, Vector3[] normals)
    {
        Array.Clear(normals);
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
    }

    public float[] Pack()
    {
        lock (_sync)
        {
            var packed = new float[_positions.Length * FloatsPerVertex];
            PackVertices(_positions, _normals, packed);
            return packed;
        }
    }

    public void PackInto(float[] packed)
    {
        lock (_sync)
        {
            if (packed.Length < _positions.Length * FloatsPerVertex)
                throw new ArgumentException("the pack buffer is smaller than the mesh", nameof(packed));
            PackVertices(_positions, _normals, packed);
        }
    }

    public static float[] PackVertices(Vector3[] positions, Vector3[] normals)
    {
        var packed = new float[positions.Length * FloatsPerVertex];
        PackVertices(positions, normals, packed);
        return packed;
    }

    public static void PackVertices(Vector3[] positions, Vector3[] normals, float[] packed)
    {
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
    }
}
