using System;

namespace BehaviourStudio.App;

internal sealed class ViewportGlState
{
    internal uint MeshProgram;
    internal uint MeshVao;
    internal uint MeshVbo;
    internal uint MeshEbo;
    internal int MeshViewProj = -1;
    internal int MeshModel = -1;
    internal int MeshKey = -1;
    internal int MeshFill = -1;
    internal int MeshBase = -1;

    internal uint LineProgram;
    internal uint GridVao;
    internal uint GridVbo;
    internal int LineViewProj = -1;
    internal int LineModel = -1;
    internal int LineBase = -1;

    internal int GridVertices;
    internal float GridExtent;
    internal uint MeshIndexCount;
    internal int UploadedVersion = -1;
    internal int[] UploadedIndices = Array.Empty<int>();
    internal int UploadedVertexFloats = -1;

    internal bool Ready => MeshProgram != 0 && LineProgram != 0;

    internal void Reset()
    {
        MeshProgram = 0;
        MeshVao = 0;
        MeshVbo = 0;
        MeshEbo = 0;
        MeshViewProj = -1;
        MeshModel = -1;
        MeshKey = -1;
        MeshFill = -1;
        MeshBase = -1;

        LineProgram = 0;
        GridVao = 0;
        GridVbo = 0;
        LineViewProj = -1;
        LineModel = -1;
        LineBase = -1;

        GridVertices = 0;
        GridExtent = 0;
        MeshIndexCount = 0;
        UploadedVersion = -1;
        UploadedIndices = Array.Empty<int>();
        UploadedVertexFloats = -1;
    }

    internal static uint EnsureProgram(uint existing, Func<uint> compile) => existing != 0 ? existing : compile();
}
