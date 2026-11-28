using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using Radicle.Interop.Native;

namespace Radicle.Interop;

using Numerics = System.Numerics;

public enum NifMeshMaterialKind : byte
{
    Lighting = 1,
    Effect = 2,
    Other = 3,
}

public enum NifMeshAlphaMode : byte
{
    Opaque = 0,
    Mask = 1,
    Blend = 2,
}

// Invariant: the basis is the column-major 3x3 with the uniform scale folded in, so Column0 is the image of the x axis.
public readonly record struct NifMeshTransform(
    Numerics.Vector3 Column0, Numerics.Vector3 Column1, Numerics.Vector3 Column2, Numerics.Vector3 Origin);

public sealed class NifMeshSkin
{
    public required string[] BoneNames { get; init; }

    public required NifMeshTransform[] InverseBinds { get; init; }

    // Invariant: four entries per vertex; an influence with weight 0 has index 0.
    public required ushort[] BoneIndices { get; init; }

    public required float[] BoneWeights { get; init; }
}

// Invariant: an attribute the NIF lacks is null, every value is in engine axes and metres, and Skin is null for a rigid part.
public sealed class NifMeshPart
{
    public required string Name { get; init; }

    public required int Block { get; init; }

    public required int VertexCount { get; init; }

    public required float[] Positions { get; init; }

    public required float[]? Normals { get; init; }

    // Constraint: four floats per vertex, xyz then the glTF handedness sign.
    public required float[]? Tangents { get; init; }

    public required float[]? Uv0 { get; init; }

    public required float[]? Uv1 { get; init; }

    public required float[]? Colors { get; init; }

    public required uint[] Indices { get; init; }

    public required Numerics.Vector3 BoundsMin { get; init; }

    public required Numerics.Vector3 BoundsMax { get; init; }

    public required Numerics.Vector3 BoundsCenter { get; init; }

    public required float BoundsRadius { get; init; }

    // Constraint: an index into NifMesh.Materials, -1 for a shape with no shader.
    public required int Material { get; init; }

    public required NifMeshSkin? Skin { get; init; }

    public required string Warning { get; init; }

    // Invariant: true only for a segmented shape whose every sub-segment user slot is in the gore-cap range.
    public bool DismemberCap { get; init; }
}

public static class NifDismemberCap
{
    public const uint FirstSlot = 100;
    public const uint LastSlot = 102;

    // Constraint: user records run segment, its subs, next segment, its subs; cap range is the game's `slot - 100 < 3` compare (RE Docs gore-caps Q7a).
    public static bool IsCap(IReadOnlyList<int> subCounts, IReadOnlyList<uint> userSlots)
    {
        int record = 0, subs = 0;
        foreach (var count in subCounts)
        {
            record++;
            for (var j = 0; j < count; j++, record++)
            {
                if (record >= userSlots.Count || userSlots[record] < FirstSlot || userSlots[record] > LastSlot) return false;
                subs++;
            }
        }
        return subs > 0;
    }
}

// Invariant: zero unless the material is a lighting shader; HasBacklight and HasGrayscaleToPaletteScale say whether the block stores that field.
public readonly record struct NifMeshLightingValues(
    float Glossiness,
    Numerics.Vector3 SpecularColor,
    float SpecularStrength,
    float Alpha,
    float FresnelPower,
    float SubsurfaceRolloff,
    float RimlightPower,
    float BacklightPower,
    bool HasBacklight,
    float GrayscaleToPaletteScale,
    bool HasGrayscaleToPaletteScale);

// Invariant: zero unless the material is an effect shader.
public readonly record struct NifMeshEffectValues(
    Numerics.Vector4 BaseColor,
    float BaseColorScale,
    float FalloffStartAngle,
    float FalloffStopAngle,
    float FalloffStartOpacity,
    float FalloffStopOpacity,
    float SoftFalloffDepth);

// Constraint: the blend factors are NiAlphaProperty enum values (0 one, 1 zero, 6 source alpha, 7 inverse source alpha) and mean something only when Present.
public readonly record struct NifMeshAlphaProperty(bool Present, byte SourceBlend, byte DestinationBlend, byte Threshold);

public readonly record struct NifMeshFlags(
    bool TwoSided,
    bool VertexColors,
    bool VertexAlpha,
    bool ZWrite,
    bool CastShadows,
    bool ModelSpaceNormals,
    bool Decal,
    bool Specular,
    bool EnvironmentMapping,
    bool OwnEmit,
    bool GlowMap,
    bool PaletteColor,
    bool Hair);

// Constraint: texture paths are the stored text, unresolved; an absent texture or root material is the empty string.
public sealed class NifMeshMaterial
{
    public required string Key { get; init; }

    public required string ShaderKind { get; init; }

    public required NifMeshMaterialKind Kind { get; init; }

    public required NifMeshAlphaMode AlphaMode { get; init; }

    public required float AlphaCutoff { get; init; }

    public required string Diffuse { get; init; }

    public required string Normal { get; init; }

    public required string Glow { get; init; }

    public required string Greyscale { get; init; }

    public required string Cubemap { get; init; }

    public required string EnvMask { get; init; }

    public required string SmoothSpec { get; init; }

    public required Numerics.Vector2 UvOffset { get; init; }

    public required Numerics.Vector2 UvScale { get; init; }

    public required Numerics.Vector3? SkinTint { get; init; }

    public required NifMeshFlags Flags { get; init; }

    public required string RootMaterial { get; init; }

    public required uint ShaderType { get; init; }

    public required uint ShaderFlags1 { get; init; }

    public required uint ShaderFlags2 { get; init; }

    public required uint TextureClamp { get; init; }

    // Invariant: an effect shader reports its base colour and scale here.
    public required Numerics.Vector3 EmissiveColor { get; init; }

    public required float EmissiveMultiple { get; init; }

    public required float EnvMapScale { get; init; }

    public required bool HasEnvMap { get; init; }

    public required NifMeshLightingValues Lighting { get; init; }

    public required NifMeshEffectValues Effect { get; init; }

    public required NifMeshAlphaProperty AlphaProperty { get; init; }
}

public sealed record NifAuditFinding(string Severity, string Rule, int Block, string Message);

public sealed class NifMesh
{
    public required NifMeshPart[] Parts { get; init; }

    public required NifMeshMaterial[] Materials { get; init; }

    // Constraint: empty unless Read was asked to audit; an audit that fails inside Radicle also reads as empty and never throws.
    public NifAuditFinding[] AuditFindings { get; private init; } = [];

    // Requires unitsPerMetre 0 (Radicle's default) or a finite positive value.
    public static unsafe NifMesh Read(ReadOnlySpan<byte> nifFile, float unitsPerMetre = 0, bool flipWinding = false, bool audit = false)
    {
        if (nifFile.IsEmpty) throw new ArgumentException("a NIF needs at least a header", nameof(nifFile));
        IntPtr nif = IntPtr.Zero;
        fixed (byte* pointer = nifFile)
        {
            Call.Check(RadicleNative.NifRead(pointer, (nuint)nifFile.Length, &nif));
        }
        try
        {
            var options = new MeshOptions { UnitsPerMetre = unitsPerMetre, Flags = flipWinding ? (uint)MeshFlag.Winding : 0u };
            Mesh* mesh = null;
            Call.Check(RadicleNative.NifMeshRead(nif, &options, &mesh));
            try
            {
                var read = Copy(mesh, nif);
                return audit ? new NifMesh { Parts = read.Parts, Materials = read.Materials, AuditFindings = ReadAudit(nif) } : read;
            }
            finally
            {
                RadicleNative.NifMeshFree(mesh);
            }
        }
        finally
        {
            RadicleNative.NifDestroy(nif);
        }
    }

    private static unsafe NifAuditFinding[] ReadAudit(IntPtr nif)
    {
        byte* text = null;
        nuint size = 0;
        if (RadicleNative.NifAuditText(nif, &text, &size) != Status.Ok) return [];
        var findings = new List<NifAuditFinding>();
        foreach (var line in Call.TakeLatin1(text, size).Split('\n', StringSplitOptions.RemoveEmptyEntries))
        {
            var column = line.Split('\t', 4);
            if (column.Length == 4 && int.TryParse(column[2], NumberStyles.Integer, CultureInfo.InvariantCulture, out var block)) findings.Add(new NifAuditFinding(column[0], column[1], block, column[3]));
        }
        return findings.ToArray();
    }

    private static unsafe NifMesh Copy(Mesh* mesh, IntPtr nif)
    {
        var parts = new NifMeshPart[checked((int)mesh->PartCount)];
        for (var index = 0; index < parts.Length; ++index) parts[index] = CopyPart((MeshPart*)mesh->Parts + index, nif);
        var materials = new NifMeshMaterial[checked((int)mesh->MaterialCount)];
        for (var index = 0; index < materials.Length; ++index) materials[index] = CopyMaterial((MeshMaterial*)mesh->Materials + index);
        return new NifMesh { Parts = parts, Materials = materials };
    }

    private static unsafe NifMeshPart CopyPart(MeshPart* part, IntPtr nif)
    {
        var vertices = checked((int)part->VertexCount);
        return new NifMeshPart {
            Name = Text(part->Name),
            Block = checked((int)part->Block),
            VertexCount = vertices,
            Positions = Floats(part->Positions, vertices * 3) ?? Array.Empty<float>(),
            Normals = Floats(part->Normals, vertices * 3),
            Tangents = Floats(part->Tangents, vertices * 4),
            Uv0 = Floats(part->Uv0, vertices * 2),
            Uv1 = Floats(part->Uv1, vertices * 2),
            Colors = Floats(part->Colors, vertices * 4),
            Indices = part->Indices == IntPtr.Zero ? Array.Empty<uint>() : Call.Copy<uint>(part->Indices, part->IndexCount),
            BoundsMin = new Numerics.Vector3(part->BoundsMin[0], part->BoundsMin[1], part->BoundsMin[2]),
            BoundsMax = new Numerics.Vector3(part->BoundsMax[0], part->BoundsMax[1], part->BoundsMax[2]),
            BoundsCenter = new Numerics.Vector3(part->BoundsCenter[0], part->BoundsCenter[1], part->BoundsCenter[2]),
            BoundsRadius = part->BoundsRadius,
            Material = part->Material,
            Skin = part->Skin == IntPtr.Zero ? null : CopySkin((MeshSkin*)part->Skin, vertices),
            Warning = Text(part->Warning),
            DismemberCap = IsDismemberCap(nif, part->Block),
        };
    }

    private static unsafe bool IsDismemberCap(IntPtr nif, uint block)
    {
        NifSegmentTable table;
        if (RadicleNative.NifReadSegments(nif, block, &table) != Status.Ok) return false;
        var subCounts = new int[checked((int)table.SegmentCount)];
        for (var segment = 0; segment < subCounts.Length; ++segment)
        {
            NifSegment record;
            if (RadicleNative.NifReadSegment(nif, block, (nuint)segment, &record) != Status.Ok) return false;
            subCounts[segment] = checked((int)record.SubSegmentCount);
        }
        var slots = new uint[checked((int)table.UserDataCount)];
        for (var index = 0; index < slots.Length; ++index)
        {
            NifSegmentUserData user;
            if (RadicleNative.NifReadSegmentUserData(nif, block, (nuint)index, &user) != Status.Ok) return false;
            slots[index] = user.UserIndex;
        }
        return NifDismemberCap.IsCap(subCounts, slots);
    }

    private static unsafe NifMeshSkin CopySkin(MeshSkin* skin, int vertices)
    {
        var bones = checked((int)skin->BoneCount);
        var names = new string[bones];
        var binds = new NifMeshTransform[bones];
        for (var bone = 0; bone < bones; ++bone)
        {
            names[bone] = Text(((IntPtr*)skin->BoneNames)[bone]);
            var bind = (MeshTransform*)skin->InverseBinds + bone;
            binds[bone] = new NifMeshTransform(
                new Numerics.Vector3(bind->Basis[0], bind->Basis[1], bind->Basis[2]),
                new Numerics.Vector3(bind->Basis[3], bind->Basis[4], bind->Basis[5]),
                new Numerics.Vector3(bind->Basis[6], bind->Basis[7], bind->Basis[8]),
                new Numerics.Vector3(bind->Origin[0], bind->Origin[1], bind->Origin[2]));
        }
        return new NifMeshSkin {
            BoneNames = names,
            InverseBinds = binds,
            BoneIndices = skin->BoneIndices == IntPtr.Zero ? Array.Empty<ushort>() : Call.Copy<ushort>(skin->BoneIndices, (nuint)vertices * 4),
            BoneWeights = Floats(skin->BoneWeights, vertices * 4) ?? Array.Empty<float>(),
        };
    }

    private static unsafe NifMeshMaterial CopyMaterial(MeshMaterial* m)
    {
        return new NifMeshMaterial {
            Key = Text(m->Key),
            ShaderKind = Text(m->ShaderKind),
            Kind = (NifMeshMaterialKind)m->Kind,
            AlphaMode = (NifMeshAlphaMode)m->AlphaMode,
            AlphaCutoff = m->AlphaCutoff,
            Diffuse = Text(m->Diffuse),
            Normal = Text(m->Normal),
            Glow = Text(m->Glow),
            Greyscale = Text(m->Greyscale),
            Cubemap = Text(m->Cubemap),
            EnvMask = Text(m->EnvMask),
            SmoothSpec = Text(m->SmoothSpec),
            UvOffset = new Numerics.Vector2(m->UvOffset[0], m->UvOffset[1]),
            UvScale = new Numerics.Vector2(m->UvScale[0], m->UvScale[1]),
            SkinTint = m->HasSkinTint != 0 ? new Numerics.Vector3(m->SkinTint[0], m->SkinTint[1], m->SkinTint[2]) : null,
            Flags = new NifMeshFlags(
                m->TwoSided != 0, m->VertexColors != 0, m->VertexAlpha != 0, m->ZWrite != 0, m->CastShadows != 0,
                m->ModelSpaceNormals != 0, m->Decal != 0, m->Specular != 0, m->EnvironmentMapping != 0, m->OwnEmit != 0,
                m->GlowMap != 0, m->PaletteColor != 0, m->Hair != 0),
            RootMaterial = Text(m->RootMaterial),
            ShaderType = m->ShaderType,
            ShaderFlags1 = m->ShaderFlags1,
            ShaderFlags2 = m->ShaderFlags2,
            TextureClamp = m->TextureClamp,
            EmissiveColor = new Numerics.Vector3(m->EmissiveColor[0], m->EmissiveColor[1], m->EmissiveColor[2]),
            EmissiveMultiple = m->EmissiveMultiple,
            EnvMapScale = m->EnvMapScale,
            HasEnvMap = m->HasEnvMap != 0,
            Lighting = new NifMeshLightingValues(
                m->Glossiness,
                new Numerics.Vector3(m->SpecularColor[0], m->SpecularColor[1], m->SpecularColor[2]),
                m->SpecularStrength, m->Alpha, m->FresnelPower, m->SubsurfaceRolloff, m->RimlightPower, m->BacklightPower,
                m->HasBacklight != 0, m->GrayscaleToPaletteScale, m->HasGrayscaleToPaletteScale != 0),
            Effect = new NifMeshEffectValues(
                new Numerics.Vector4(m->EffectBaseColor[0], m->EffectBaseColor[1], m->EffectBaseColor[2], m->EffectBaseColor[3]),
                m->EffectBaseColorScale, m->EffectFalloffStartAngle, m->EffectFalloffStopAngle, m->EffectFalloffStartOpacity,
                m->EffectFalloffStopOpacity, m->EffectSoftFalloffDepth),
            AlphaProperty = new NifMeshAlphaProperty(m->HasAlphaProperty != 0, m->AlphaSourceBlend, m->AlphaDestinationBlend, m->AlphaThreshold),
        };
    }

    private static float[]? Floats(IntPtr values, int count) => values == IntPtr.Zero ? null : Call.Copy<float>(values, (nuint)count);

    private static unsafe string Text(IntPtr text)
    {
        if (text == IntPtr.Zero) return string.Empty;
        var bytes = (byte*)text;
        var length = 0;
        while (bytes[length] != 0) ++length;
        return Encoding.Latin1.GetString(bytes, length);
    }
}

public sealed class TriMorphFile : IDisposable
{
    private IntPtr _handle;

    private TriMorphFile(IntPtr handle, int vertexCount, int triangleCount, string[] morphNames)
    {
        _handle = handle;
        VertexCount = vertexCount;
        TriangleCount = triangleCount;
        MorphNames = morphNames;
    }

    public int VertexCount { get; }

    public int TriangleCount { get; }

    public IReadOnlyList<string> MorphNames { get; }

    public static unsafe TriMorphFile Read(ReadOnlySpan<byte> bytes)
    {
        if (bytes.IsEmpty) throw new ArgumentException("a tri file is required", nameof(bytes));
        IntPtr handle = IntPtr.Zero;
        fixed (byte* pointer = bytes)
        {
            Call.Check(RadicleNative.TriRead(pointer, (nuint)bytes.Length, &handle));
        }
        try
        {
            uint vertices = 0, triangles = 0, morphs = 0;
            Call.Check(RadicleNative.TriCounts(handle, &vertices, &triangles, &morphs));
            var names = new string[checked((int)morphs)];
            for (var i = 0; i < names.Length; i++)
            {
                IntPtr name = IntPtr.Zero;
                Call.Check(RadicleNative.TriMorphName(handle, (uint)i, &name));
                names[i] = Utf8.Read(name)!;
            }
            return new TriMorphFile(handle, checked((int)vertices), checked((int)triangles), names);
        }
        catch
        {
            RadicleNative.TriFree(handle);
            throw;
        }
    }

    public int IndexOf(string morph)
    {
        for (var i = 0; i < MorphNames.Count; i++)
            if (string.Equals(MorphNames[i], morph, StringComparison.Ordinal)) return i;
        return -1;
    }

    public unsafe float[] Positions(float unitsPerMetre = 0f)
    {
        ObjectDisposedException.ThrowIf(_handle == IntPtr.Zero, this);
        float* values = null;
        nuint count = 0;
        Call.Check(RadicleNative.TriPositions(_handle, unitsPerMetre, &values, &count));
        return Call.TakeFloats(values, count);
    }

    public unsafe float[] Deltas(int morph, float unitsPerMetre = 0f)
    {
        ObjectDisposedException.ThrowIf(_handle == IntPtr.Zero, this);
        if ((uint)morph >= (uint)MorphNames.Count) throw new ArgumentOutOfRangeException(nameof(morph));
        float* values = null;
        nuint count = 0;
        Call.Check(RadicleNative.TriMorphDeltas(_handle, (uint)morph, unitsPerMetre, &values, &count));
        return Call.TakeFloats(values, count);
    }

    public void Dispose()
    {
        if (_handle != IntPtr.Zero) { RadicleNative.TriFree(_handle); _handle = IntPtr.Zero; }
        GC.SuppressFinalize(this);
    }

    ~TriMorphFile() { if (_handle != IntPtr.Zero) RadicleNative.TriFree(_handle); }
}
