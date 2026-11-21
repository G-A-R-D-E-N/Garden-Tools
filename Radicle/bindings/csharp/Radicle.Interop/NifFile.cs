using System;
using System.Collections.Generic;
using System.Text;
using Radicle.Interop.Native;

namespace Radicle.Interop;

using Numerics = System.Numerics;

public sealed class NifBone
{
    public required string Name { get; init; }

    public required Numerics.Vector3 BoundsCenter { get; init; }

    public required float BoundsRadius { get; init; }

    public required float[] Rotation { get; init; }

    public required Numerics.Vector3 Translation { get; init; }

    public required float Scale { get; init; }

    public Numerics.Matrix4x4 BindMatrix => new(
        Rotation[0] * Scale, Rotation[1] * Scale, Rotation[2] * Scale, 0,
        Rotation[3] * Scale, Rotation[4] * Scale, Rotation[5] * Scale, 0,
        Rotation[6] * Scale, Rotation[7] * Scale, Rotation[8] * Scale, 0,
        Translation.X, Translation.Y, Translation.Z, 1);
}

public sealed class NifNode
{
    public required int Block { get; init; }

    public required string Type { get; init; }

    public required string Name { get; init; }

    public required int ParentIndex { get; init; }

    public required int[] ChildBlocks { get; init; }

    public required NifNode?[] Children { get; init; }

    public required Numerics.Vector3 Translation { get; init; }

    public required float[] Rotation { get; init; }

    public required float Scale { get; init; }

    public int? Value { get; init; }

    public NifNode? Parent { get; internal set; }

    public bool IsRoot => ParentIndex < 0;

    public Numerics.Matrix4x4 LocalMatrix => new(
        Rotation[0] * Scale, Rotation[3] * Scale, Rotation[6] * Scale, 0,
        Rotation[1] * Scale, Rotation[4] * Scale, Rotation[7] * Scale, 0,
        Rotation[2] * Scale, Rotation[5] * Scale, Rotation[8] * Scale, 0,
        Translation.X, Translation.Y, Translation.Z, 1);

    internal int NodeCount { get; set; } = 1;

    public Numerics.Matrix4x4 WorldMatrix
    {
        get
        {
            var world = LocalMatrix;
            var current = Parent;
            for (int steps = 0; current != null; steps++)
            {
                if (steps >= NodeCount) throw new InvalidOperationException("the node's parent chain contains a cycle");
                world *= current.LocalMatrix;
                current = current.Parent;
            }
            return world;
        }
    }
}

public sealed class NifShape
{
    public required int Block { get; init; }

    public required string Name { get; init; }

    public required Numerics.Vector3[] Positions { get; init; }

    public required Numerics.Vector3[] Normals { get; init; }

    public required Numerics.Vector3[] Tangents { get; init; }

    public required Numerics.Vector3[] Bitangents { get; init; }

    public required Numerics.Vector2[] Uvs { get; init; }

    public required Numerics.Vector4[] Colors { get; init; }

    public required ushort[] Triangles { get; init; }

    public required string Material { get; init; }

    public required string[] Textures { get; init; }

    public required float[] Weights { get; init; }

    public required byte[] BoneIndices { get; init; }

    public required NifBone[] Bones { get; init; }

    public required int ParentNodeIndex { get; init; }

    public NifNode? ParentNode { get; internal set; }

    public required string Warning { get; init; }

    public required int SkinReference { get; init; }

    public required int ShaderReference { get; init; }

    public required int AlphaReference { get; init; }

    public required Numerics.Vector3 GlobalTranslation { get; init; }

    public required float[] GlobalRotation { get; init; }

    public required float GlobalScale { get; init; }

    public int NativeVertexCount { get; init; }

    public bool HasSkinWeights { get; init; }

    public int VertexCount => Positions.Length;

    public int TriangleCount => Triangles.Length / 3;

    public bool IsSkinned => Weights.Length == Positions.Length * 4 && Bones.Length > 0;
}

public sealed class NifCollisionBlob
{
    public required int Block { get; init; }

    public required string Type { get; init; }

    public required byte[] Bytes { get; init; }

    // Invariant: every owner field is the reader's "none" value when no bhkNPCollisionObject names this system.
    public required int OwnerBlock { get; init; }

    public required ushort Flags { get; init; }

    // Constraint: equals Block whenever OwnerBlock is set, because Data must name a system block.
    public required int SystemRef { get; init; }

    public required uint BodyId { get; init; }

    public required int TargetBlock { get; init; }

    public required int TargetNodeIndex { get; init; }

    public NifNode? TargetNode { get; init; }

    // Invariant: one entry per bhkNPCollisionObject naming this system in block order; entry 0 matches the scalar fields above.
    public IReadOnlyList<NifCollisionOwnerInfo> Owners { get; init; } = [];
}

public sealed class NifSubSegmentRecord
{
    public required uint StartIndex { get; init; }

    public required uint Primitives { get; init; }

    public required uint ParentArrayIndex { get; init; }

    public required uint Unused { get; init; }
}

public sealed class NifSegmentRecord
{
    public required uint StartIndex { get; init; }

    public required uint Primitives { get; init; }

    public required uint ParentArrayIndex { get; init; }

    public required NifSubSegmentRecord[] SubSegments { get; init; }
}

public sealed class NifSegmentUser
{
    public required uint UserIndex { get; init; }

    public required uint BoneId { get; init; }

    public required float[] CutOffsets { get; init; }
}

public sealed class NifSegmentSet
{
    public required uint Primitives { get; init; }

    public required uint TotalSegments { get; init; }

    public required NifSegmentRecord[] Segments { get; init; }

    public required uint[] SegmentStarts { get; init; }

    public required NifSegmentUser[] UserData { get; init; }

    public required string SsfFile { get; init; }
}

public sealed class NifLightingShaderInfo
{
    public required int Block { get; init; }

    public required string Name { get; init; }

    public required uint ShaderType { get; init; }

    public required uint Flags1 { get; init; }

    public required uint Flags2 { get; init; }

    public required Numerics.Vector2 UvOffset { get; init; }

    public required Numerics.Vector2 UvScale { get; init; }

    public required int TextureSetRef { get; init; }

    public required int TextureCount { get; init; }

    public required Numerics.Vector3 EmissiveColor { get; init; }

    public required float EmissiveMultiple { get; init; }

    public required bool HasRootMaterial { get; init; }

    public required string RootMaterial { get; init; }

    public required uint TextureClamp { get; init; }

    public required float Alpha { get; init; }

    public required float RefractionStrength { get; init; }

    public required float Smoothness { get; init; }

    public required Numerics.Vector3 SpecularColor { get; init; }

    public required float SpecularStrength { get; init; }

    public required float SubsurfaceRolloff { get; init; }

    public required float RimlightPower { get; init; }

    public required float FresnelPower { get; init; }

    public required float? Backlight { get; init; }

    public required float? GrayscaleToPaletteScale { get; init; }

    public required float[]? Wetness { get; init; }

    public required bool HasEnvMap { get; init; }

    public required float EnvMapScale { get; init; }

    public required bool EnvMapSsr { get; init; }

    public required bool EnvMapWetnessSsr { get; init; }

    public required float[]? SkinTint { get; init; }

    public required float[]? HairTint { get; init; }

    public required float[]? Snow { get; init; }

    public required float[]? ParallaxOcc { get; init; }

    public required float[]? MultiLayerParallax { get; init; }

    public required bool HasEye { get; init; }

    public required float EyeCubemapScale { get; init; }

    public required Numerics.Vector3 EyeLeft { get; init; }

    public required Numerics.Vector3 EyeRight { get; init; }

    public required int TailBytes { get; init; }
}

public sealed class NifLightInfo
{
    public required int Block { get; init; }

    public required int ParentBlock { get; init; }

    public required uint Flags { get; init; }

    public required string Name { get; init; }

    public required string Type { get; init; }

    public required Numerics.Vector3 Translation { get; init; }

    public required float[] Rotation { get; init; }

    public required float Scale { get; init; }
}

public sealed class NifEffectShaderInfo
{
    public required int Block { get; init; }

    public required string Name { get; init; }

    public required uint Flags1 { get; init; }

    public required uint Flags2 { get; init; }

    public required Numerics.Vector2 UvOffset { get; init; }

    public required Numerics.Vector2 UvScale { get; init; }

    public required byte TextureClamp { get; init; }

    public required byte LightingInfluence { get; init; }

    public required byte EnvMapMinLod { get; init; }

    public required float FalloffStartAngle { get; init; }

    public required float FalloffStopAngle { get; init; }

    public required float FalloffStartOpacity { get; init; }

    public required float FalloffStopOpacity { get; init; }

    public required Numerics.Vector4 BaseColor { get; init; }

    public required float BaseColorScale { get; init; }

    public required float SoftFalloffDepth { get; init; }

    public required string[] Textures { get; init; }

    public required float EnvMapScale { get; init; }
}

public sealed class NifAlphaPropertyInfo
{
    public required int Block { get; init; }

    public required ushort Flags { get; init; }

    public required byte Threshold { get; init; }

    public required bool BlendEnabled { get; init; }

    public required byte SourceBlend { get; init; }

    public required byte DestinationBlend { get; init; }

    public required bool TestEnabled { get; init; }

    public required byte TestFunction { get; init; }
}

public sealed class NifFile
{
    private readonly byte[] _bytes;

    private NifFile(byte[] bytes, NifNode[] nodes, NifShape[] shapes, NifCollisionBlob[] collisions)
    {
        _bytes = bytes;
        Nodes = nodes;
        Shapes = shapes;
        Collisions = collisions;
    }

    public IReadOnlyList<NifNode> Nodes { get; }

    public IReadOnlyList<NifShape> Shapes { get; }

    public IReadOnlyList<NifCollisionBlob> Collisions { get; }

    public IReadOnlyList<NifLightInfo> Lights { get; private init; } = Array.Empty<NifLightInfo>();

    public static unsafe IReadOnlyList<string> ShaderFlagNames
    {
        get
        {
            var names = new string[checked((int)RadicleNative.NifShaderFlagCount())];
            for (var bit = 0; bit < names.Length; ++bit)
            {
                byte* text = null;
                nuint size = 0;
                Call.Check(RadicleNative.NifShaderFlagName((uint)bit, &text, &size));
                names[bit] = Call.TakeLatin1(text, size);
            }
            return names;
        }
    }

    public static unsafe NifFile FromBytes(ReadOnlySpan<byte> bytes)
    {
        if (bytes.IsEmpty) throw new ArgumentException("a NIF needs at least a header", nameof(bytes));
        var handle = Open(bytes);
        try
        {
            var nodes = ReadNodes(handle);
            var shapes = ReadShapes(handle, nodes);
            var collisions = ReadCollisions(handle, nodes);
            return new NifFile(bytes.ToArray(), nodes, shapes, collisions) { Lights = ReadLights(handle) };
        }
        finally
        {
            RadicleNative.NifDestroy(handle);
        }
    }

    private static unsafe IntPtr Open(ReadOnlySpan<byte> bytes)
    {
        IntPtr handle = IntPtr.Zero;
        fixed (byte* pointer = bytes)
        {
            Call.Check(RadicleNative.NifRead(pointer, (nuint)bytes.Length, &handle));
        }
        return handle;
    }

    /// <summary>Requires shapeIndex to index <see cref="Shapes"/>; an unreadable shader or material is a warning on the result, not an exception. A swap re-resolves from its replacement when its original names the shape's material.</summary>
    public ResolvedMaterial ResolveMaterial(int shapeIndex, AssetResolver assets, IReadOnlyList<MaterialSubstitution>? swap = null)
    {
        var handle = Open(_bytes);
        try
        {
            return ResolvedMaterial.Resolve(handle, shapeIndex, assets, swap);
        }
        finally
        {
            RadicleNative.NifDestroy(handle);
        }
    }

    private T Read<T>(int block, Func<IntPtr, uint, T> read)
    {
        if (block < 0) throw new ArgumentOutOfRangeException(nameof(block));
        var handle = Open(_bytes);
        try
        {
            return read(handle, (uint)block);
        }
        finally
        {
            RadicleNative.NifDestroy(handle);
        }
    }

    public unsafe NifSegmentSet? ReadSegments(int block)
    {
        return Read<NifSegmentSet?>(block, (handle, id) =>
        {
            NifSegmentTable table;
            if (!Call.Found(RadicleNative.NifReadSegments(handle, id, &table))) return null;

            var segments = new NifSegmentRecord[checked((int)table.SegmentCount)];
            for (var index = 0; index < segments.Length; ++index)
            {
                NifSegment segment;
                Call.Check(RadicleNative.NifReadSegment(handle, id, (nuint)index, &segment));
                var subs = new NifSubSegmentRecord[checked((int)segment.SubSegmentCount)];
                for (var sub = 0; sub < subs.Length; ++sub)
                {
                    NifSubSegment entry;
                    Call.Check(RadicleNative.NifReadSubSegment(handle, id, (nuint)index, (nuint)sub, &entry));
                    subs[sub] = new NifSubSegmentRecord {
                        StartIndex = entry.StartIndex,
                        Primitives = entry.Primitives,
                        ParentArrayIndex = entry.ParentArrayIndex,
                        Unused = entry.Unused,
                    };
                }
                segments[index] = new NifSegmentRecord {
                    StartIndex = segment.StartIndex,
                    Primitives = segment.Primitives,
                    ParentArrayIndex = segment.ParentArrayIndex,
                    SubSegments = subs,
                };
            }

            var starts = new uint[checked((int)table.SegmentStartCount)];
            for (var index = 0; index < starts.Length; ++index)
            {
                uint start;
                Call.Check(RadicleNative.NifSegmentStart(handle, id, (nuint)index, &start));
                starts[index] = start;
            }

            var users = new NifSegmentUser[checked((int)table.UserDataCount)];
            for (var index = 0; index < users.Length; ++index)
            {
                NifSegmentUserData user;
                Call.Check(RadicleNative.NifReadSegmentUserData(handle, id, (nuint)index, &user));
                float* offsets = null;
                nuint offsetCount = 0;
                Call.Check(RadicleNative.NifSegmentCutOffsets(handle, id, (nuint)index, &offsets, &offsetCount));
                users[index] = new NifSegmentUser {
                    UserIndex = user.UserIndex,
                    BoneId = user.BoneId,
                    CutOffsets = Call.TakeFloats(offsets, offsetCount),
                };
            }

            byte* ssf = null;
            nuint ssfSize = 0;
            Call.Check(RadicleNative.NifSegmentSsfFile(handle, id, &ssf, &ssfSize));
            return new NifSegmentSet {
                Primitives = table.Primitives,
                TotalSegments = table.TotalSegments,
                Segments = segments,
                SegmentStarts = starts,
                UserData = users,
                SsfFile = Call.TakeLatin1(ssf, ssfSize),
            };
        });
    }

    public unsafe NifLightingShaderInfo ReadLightingShader(int block)
    {
        return Read(block, (handle, id) =>
        {
            NifLightingShader shader;
            Call.Check(RadicleNative.NifReadLightingShader(handle, id, &shader));

            byte* name = null;
            nuint nameSize = 0;
            Call.Check(RadicleNative.NifLightingShaderName(handle, id, &name, &nameSize));
            byte* root = null;
            nuint rootSize = 0;
            Call.Check(RadicleNative.NifLightingShaderRootMaterial(handle, id, &root, &rootSize));

            return new NifLightingShaderInfo {
                Block = checked((int)shader.Block),
                Name = Call.TakeLatin1(name, nameSize),
                ShaderType = shader.ShaderType,
                Flags1 = shader.Flags1,
                Flags2 = shader.Flags2,
                UvOffset = new Numerics.Vector2(shader.UvOffset[0], shader.UvOffset[1]),
                UvScale = new Numerics.Vector2(shader.UvScale[0], shader.UvScale[1]),
                TextureSetRef = shader.TextureSetRef,
                TextureCount = checked((int)shader.TextureCount),
                EmissiveColor = new Numerics.Vector3(shader.EmissiveColor[0], shader.EmissiveColor[1], shader.EmissiveColor[2]),
                EmissiveMultiple = shader.EmissiveMultiple,
                HasRootMaterial = shader.HasRootMaterial != 0,
                RootMaterial = Call.TakeLatin1(root, rootSize),
                TextureClamp = shader.TextureClamp,
                Alpha = shader.Alpha,
                RefractionStrength = shader.RefractionStrength,
                Smoothness = shader.Smoothness,
                SpecularColor = new Numerics.Vector3(shader.SpecularColor[0], shader.SpecularColor[1], shader.SpecularColor[2]),
                SpecularStrength = shader.SpecularStrength,
                SubsurfaceRolloff = shader.SubsurfaceRolloff,
                RimlightPower = shader.RimlightPower,
                FresnelPower = shader.FresnelPower,
                Backlight = shader.HasBacklight != 0 ? shader.BacklightPower : null,
                GrayscaleToPaletteScale = shader.HasGrayscaleToPaletteScale != 0 ? shader.GrayscaleToPaletteScale : null,
                Wetness = shader.HasWetness != 0 ? Call.Copy<float>((IntPtr)shader.Wetness, 6) : null,
                HasEnvMap = shader.HasEnvMap != 0,
                EnvMapScale = shader.EnvMapScale,
                EnvMapSsr = shader.EnvMapSsr != 0,
                EnvMapWetnessSsr = shader.EnvMapWetnessSsr != 0,
                SkinTint = shader.HasSkinTint != 0 ? Call.Copy<float>((IntPtr)shader.SkinTint, 4) : null,
                HairTint = shader.HasHairTint != 0 ? Call.Copy<float>((IntPtr)shader.HairTint, 3) : null,
                Snow = shader.HasSnow != 0 ? Call.Copy<float>((IntPtr)shader.Snow, 4) : null,
                ParallaxOcc = shader.HasParallaxOcc != 0 ? Call.Copy<float>((IntPtr)shader.ParallaxOcc, 2) : null,
                MultiLayerParallax = shader.HasMultiLayerParallax != 0
                    ? Call.Copy<float>((IntPtr)shader.MultiLayerParallax, 5) : null,
                HasEye = shader.HasEye != 0,
                EyeCubemapScale = shader.EyeCubemapScale,
                EyeLeft = new Numerics.Vector3(shader.EyeLeft[0], shader.EyeLeft[1], shader.EyeLeft[2]),
                EyeRight = new Numerics.Vector3(shader.EyeRight[0], shader.EyeRight[1], shader.EyeRight[2]),
                TailBytes = checked((int)shader.TailBytes),
            };
        });
    }

    public unsafe NifEffectShaderInfo ReadEffectShader(int block)
    {
        return Read(block, (handle, id) =>
        {
            NifEffectShader shader;
            Call.Check(RadicleNative.NifReadEffectShader(handle, id, &shader));

            var textures = new string[checked((int)shader.TextureCount)];
            for (var slot = 0; slot < textures.Length; ++slot)
            {
                byte* text = null;
                nuint size = 0;
                Call.Check(RadicleNative.NifEffectShaderTexture(handle, id, (nuint)slot, &text, &size));
                textures[slot] = Call.TakeLatin1(text, size);
            }

            // Invariant: the name buffer is read last so a failing texture slot read cannot leave it unreleased.
            byte* name = null;
            nuint nameSize = 0;
            Call.Check(RadicleNative.NifEffectShaderName(handle, id, &name, &nameSize));

            return new NifEffectShaderInfo {
                Block = checked((int)shader.Block),
                Name = Call.TakeLatin1(name, nameSize),
                Flags1 = shader.Flags1,
                Flags2 = shader.Flags2,
                UvOffset = new Numerics.Vector2(shader.UvOffset[0], shader.UvOffset[1]),
                UvScale = new Numerics.Vector2(shader.UvScale[0], shader.UvScale[1]),
                TextureClamp = shader.TextureClamp,
                LightingInfluence = shader.LightingInfluence,
                EnvMapMinLod = shader.EnvMapMinLod,
                FalloffStartAngle = shader.FalloffStartAngle,
                FalloffStopAngle = shader.FalloffStopAngle,
                FalloffStartOpacity = shader.FalloffStartOpacity,
                FalloffStopOpacity = shader.FalloffStopOpacity,
                BaseColor = new Numerics.Vector4(shader.BaseColor[0], shader.BaseColor[1], shader.BaseColor[2], shader.BaseColor[3]),
                BaseColorScale = shader.BaseColorScale,
                SoftFalloffDepth = shader.SoftFalloffDepth,
                Textures = textures,
                EnvMapScale = shader.EnvMapScale,
            };
        });
    }

    public unsafe NifAlphaPropertyInfo ReadAlphaProperty(int block)
    {
        return Read(block, (handle, id) =>
        {
            NifAlphaProperty alpha;
            Call.Check(RadicleNative.NifReadAlphaProperty(handle, id, &alpha));
            return new NifAlphaPropertyInfo {
                Block = checked((int)alpha.Block),
                Flags = alpha.Flags,
                Threshold = alpha.Threshold,
                BlendEnabled = alpha.BlendEnabled != 0,
                SourceBlend = alpha.SourceBlend,
                DestinationBlend = alpha.DestinationBlend,
                TestEnabled = alpha.TestEnabled != 0,
                TestFunction = alpha.TestFunction,
            };
        });
    }

    private static unsafe NifLightInfo[] ReadLights(IntPtr handle)
    {
        nuint count = 0;
        Call.Check(RadicleNative.NifLightCount(handle, &count));
        var lights = new NifLightInfo[checked((int)count)];
        for (var index = 0; index < lights.Length; ++index)
        {
            NifLight light;
            Call.Check(RadicleNative.NifReadLight(handle, (nuint)index, &light));
            lights[index] = new NifLightInfo {
                Block = checked((int)light.Block),
                ParentBlock = light.ParentBlock,
                Flags = light.Flags,
                Name = ReadText(handle, (nuint)index, RadicleNative.NifLightName),
                Type = ReadText(handle, (nuint)index, RadicleNative.NifLightType),
                Translation = new Numerics.Vector3(light.Translation[0], light.Translation[1], light.Translation[2]),
                Rotation = Call.Copy<float>((IntPtr)light.Rotation, 9),
                Scale = light.Scale,
            };
        }
        return lights;
    }

    private static unsafe NifNode[] ReadNodes(IntPtr handle)
    {
        nuint nodeCount = 0;
        Call.Check(RadicleNative.NifNodeCount(handle, &nodeCount));
        var nodes = new NifNode[checked((int)nodeCount)];
        var childBlocks = new int[nodes.Length][];
        var translation = stackalloc float[3];
        var rotation = stackalloc float[9];
        for (var index = 0; index < nodes.Length; ++index)
        {
            uint block = 0;
            Call.Check(RadicleNative.NifNodeBlock(handle, (nuint)index, &block));
            int parent = -1;
            Call.Check(RadicleNative.NifNodeParent(handle, (nuint)index, &parent));

            uint* refs = null;
            nuint refCount = 0;
            Call.Check(RadicleNative.NifNodeChildren(handle, (nuint)index, &refs, &refCount));
            var blocks = Call.TakeNodeRefs(refs, refCount);

            float scale = 0;
            Call.Check(RadicleNative.NifNodeTransform(handle, (nuint)index, translation, rotation, &scale));

            int hasValue = 0;
            int nodeValue = 0;
            Call.Check(RadicleNative.NifNodeValue(handle, (nuint)index, &hasValue, &nodeValue));

            childBlocks[index] = Array.ConvertAll(blocks, value => checked((int)value));
            nodes[index] = new NifNode {
                Block = checked((int)block),
                Type = ReadText(handle, (nuint)index, RadicleNative.NifNodeType),
                Name = ReadText(handle, (nuint)index, RadicleNative.NifNodeName),
                ParentIndex = parent,
                ChildBlocks = childBlocks[index],
                Children = new NifNode?[childBlocks[index].Length],
                Translation = new Numerics.Vector3(translation[0], translation[1], translation[2]),
                Rotation = Call.Copy<float>((IntPtr)rotation, 9),
                Scale = scale,
                Value = hasValue != 0 ? nodeValue : null,
            };
        }

        for (var index = 0; index < nodes.Length; ++index)
        {
            nodes[index].NodeCount = nodes.Length;
            if (nodes[index].ParentIndex >= 0) nodes[index].Parent = nodes[nodes[index].ParentIndex];
            for (var slot = 0; slot < nodes[index].ChildBlocks.Length; ++slot)
            {
                int child = -1;
                Call.Check(RadicleNative.NifNodeForBlock(handle, (uint)nodes[index].ChildBlocks[slot], &child));
                if (child >= 0) nodes[index].Children[slot] = nodes[child];
            }
        }
        return nodes;
    }

    private static unsafe NifCollisionBlob[] ReadCollisions(IntPtr handle, NifNode[] nodes)
    {
        nuint count = 0;
        Call.Check(RadicleNative.NifCollisionCount(handle, &count));
        var collisions = new NifCollisionBlob[checked((int)count)];
        for (var index = 0; index < collisions.Length; ++index)
        {
            uint block = 0;
            Call.Check(RadicleNative.NifCollisionBlock(handle, (nuint)index, &block));

            byte* type = null;
            nuint typeSize = 0;
            Call.Check(RadicleNative.NifCollisionType(handle, (nuint)index, &type, &typeSize));

            byte* payload = null;
            nuint payloadSize = 0;
            Call.Check(RadicleNative.NifCollisionBytes(handle, (nuint)index, &payload, &payloadSize));

            int ownerBlock = -1;
            Call.Check(RadicleNative.NifCollisionOwnerBlock(handle, (nuint)index, &ownerBlock));

            ushort flags = 0;
            Call.Check(RadicleNative.NifCollisionFlags(handle, (nuint)index, &flags));

            int systemRef = -1;
            Call.Check(RadicleNative.NifCollisionSystemRef(handle, (nuint)index, &systemRef));

            uint bodyId = 0;
            Call.Check(RadicleNative.NifCollisionBodyId(handle, (nuint)index, &bodyId));

            int targetBlock = -1;
            Call.Check(RadicleNative.NifCollisionTargetBlock(handle, (nuint)index, &targetBlock));

            int targetNode = -1;
            Call.Check(RadicleNative.NifCollisionTargetNode(handle, (nuint)index, &targetNode));

            collisions[index] = new NifCollisionBlob {
                Block = checked((int)block),
                Type = Call.TakeLatin1(type, typeSize),
                Bytes = Call.TakeBytes(payload, payloadSize),
                OwnerBlock = ownerBlock,
                Flags = flags,
                SystemRef = systemRef,
                BodyId = bodyId,
                TargetBlock = targetBlock,
                TargetNodeIndex = targetNode,
                TargetNode = targetNode >= 0 && targetNode < nodes.Length ? nodes[targetNode] : null,
                Owners = NifCollisionOwners.Read(handle, (nuint)index, nodes),
            };
        }
        return collisions;
    }

    private static unsafe NifShape[] ReadShapes(IntPtr handle, NifNode[] nodes)
    {
        nuint shapeCount = 0;
        Call.Check(RadicleNative.NifShapeCount(handle, &shapeCount));
        var shapes = new NifShape[checked((int)shapeCount)];
        for (var index = 0; index < shapes.Length; ++index) shapes[index] = Snapshot(handle, nodes, (nuint)index);
        return shapes;
    }

    private unsafe delegate Status UshortReader(IntPtr nif, nuint index, ushort** values, nuint* count);

    private unsafe delegate Status ByteReader(IntPtr nif, nuint index, byte** values, nuint* count);

    private static unsafe ushort[] ReadUshorts(IntPtr handle, nuint index, UshortReader reader)
    {
        ushort* values = null;
        nuint count = 0;
        Call.Check(reader(handle, index, &values, &count));
        return Call.TakeTriangles(values, count);
    }

    private static unsafe byte[] ReadBytes(IntPtr handle, nuint index, ByteReader reader)
    {
        byte* values = null;
        nuint count = 0;
        Call.Check(reader(handle, index, &values, &count));
        return Call.TakeBytes(values, count);
    }

    private static string ReadText(IntPtr handle, nuint index, ByteReader reader) =>
        Encoding.Latin1.GetString(ReadBytes(handle, index, reader));

    private static Numerics.Vector2[] ToTexCoords(float[] values)
    {
        var result = new Numerics.Vector2[values.Length / 2];
        for (var i = 0; i < result.Length; ++i) result[i] = new Numerics.Vector2(values[i * 2], values[i * 2 + 1]);
        return result;
    }

    private static Numerics.Vector4[] ToColors(float[] values)
    {
        var result = new Numerics.Vector4[values.Length / 4];
        for (var i = 0; i < result.Length; ++i) {
            result[i] = new Numerics.Vector4(values[i * 4], values[i * 4 + 1], values[i * 4 + 2], values[i * 4 + 3]);
        }
        return result;
    }

    private static unsafe NifBone[] ReadBones(IntPtr handle, nuint index)
    {
        nuint boneCount = 0;
        Call.Check(RadicleNative.NifShapeBoneCount(handle, index, &boneCount));
        var bones = new NifBone[checked((int)boneCount)];
        if (bones.Length == 0) return bones;

        var center = stackalloc float[3];
        var rotation = stackalloc float[9];
        var translation = stackalloc float[3];
        for (var bone = 0; bone < bones.Length; ++bone) {
            byte* name = null;
            nuint nameSize = 0;
            Call.Check(RadicleNative.NifShapeBoneName(handle, index, (nuint)bone, &name, &nameSize));
            var boneName = Call.TakeLatin1(name, nameSize);

            float radius = 0;
            float scale = 0;
            Call.Check(RadicleNative.NifShapeBoneTransform(handle, index, (nuint)bone, center, &radius, rotation,
                translation, &scale));
            bones[bone] = new NifBone {
                Name = boneName,
                BoundsCenter = new Numerics.Vector3(center[0], center[1], center[2]),
                BoundsRadius = radius,
                Rotation = Call.Copy<float>((IntPtr)rotation, 9),
                Translation = new Numerics.Vector3(translation[0], translation[1], translation[2]),
                Scale = scale,
            };
        }
        return bones;
    }

    private static unsafe string[] ReadTextures(IntPtr handle, nuint index)
    {
        nuint textureCount = 0;
        Call.Check(RadicleNative.NifShapeTextureCount(handle, index, &textureCount));
        var textures = new string[checked((int)textureCount)];
        for (var slot = 0; slot < textures.Length; ++slot) {
            byte* text = null;
            nuint size = 0;
            Call.Check(RadicleNative.NifShapeTexture(handle, index, (nuint)slot, &text, &size));
            textures[slot] = Call.TakeLatin1(text, size);
        }
        return textures;
    }

    private static unsafe NifShape Snapshot(IntPtr handle, NifNode[] nodes, nuint index)
    {
        int skin = 0;
        int shader = 0;
        int alpha = 0;
        Call.Check(RadicleNative.NifShapeRefs(handle, index, &skin, &shader, &alpha));

        int parentNode = -1;
        Call.Check(RadicleNative.NifShapeParentNode(handle, index, &parentNode));

        var translation = stackalloc float[3];
        var rotation = stackalloc float[9];
        float scale = 0;
        Call.Check(RadicleNative.NifShapeTransform(handle, index, translation, rotation, &scale));

        uint block = 0;
        Call.Check(RadicleNative.NifShapeBlock(handle, index, &block));

        nuint vertexCount = 0;
        Call.Check(RadicleNative.NifShapeVertexCount(handle, index, &vertexCount));
        int skinned = 0;
        Call.Check(RadicleNative.NifShapeSkinned(handle, index, &skinned));

        var shape = new NifShape {
            Block = checked((int)block),
            Name = ReadText(handle, index, RadicleNative.NifShapeName),
            Positions = Call.Reinterpret<Numerics.Vector3>(Call.ReadFloats(handle, index, RadicleNative.NifShapePositions)),
            Normals = Call.Reinterpret<Numerics.Vector3>(Call.ReadFloats(handle, index, RadicleNative.NifShapeNormals)),
            Tangents = Call.Reinterpret<Numerics.Vector3>(Call.ReadFloats(handle, index, RadicleNative.NifShapeTangents)),
            Bitangents = Call.Reinterpret<Numerics.Vector3>(Call.ReadFloats(handle, index, RadicleNative.NifShapeBitangents)),
            Uvs = ToTexCoords(Call.ReadFloats(handle, index, RadicleNative.NifShapeUvs)),
            Colors = ToColors(Call.ReadFloats(handle, index, RadicleNative.NifShapeColors)),
            Triangles = ReadUshorts(handle, index, RadicleNative.NifShapeTriangles),
            Material = ReadText(handle, index, RadicleNative.NifShapeMaterial),
            Textures = ReadTextures(handle, index),
            Weights = Call.ReadFloats(handle, index, RadicleNative.NifShapeWeights),
            BoneIndices = ReadBytes(handle, index, RadicleNative.NifShapeBoneIndices),
            Bones = ReadBones(handle, index),
            ParentNodeIndex = parentNode,
            Warning = ReadText(handle, index, RadicleNative.NifShapeWarning),
            SkinReference = skin,
            ShaderReference = shader,
            AlphaReference = alpha,
            GlobalTranslation = new Numerics.Vector3(translation[0], translation[1], translation[2]),
            GlobalRotation = Call.Copy<float>((IntPtr)rotation, 9),
            GlobalScale = scale,
            NativeVertexCount = checked((int)vertexCount),
            HasSkinWeights = skinned != 0,
        };
        if (parentNode >= 0) shape.ParentNode = nodes[parentNode];
        return shape;
    }
}
