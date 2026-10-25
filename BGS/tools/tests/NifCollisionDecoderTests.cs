using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using OpenCommonwealth.Services.Hkx;
using Radicle.Interop;
using Xunit;

namespace BehaviourStudio.Tests;

// Invariant: the decoder fails closed on a truncated or hostile blob and never depends on a fixed offset.
public sealed class NifCollisionDecoderTests
{
    public sealed class NifCorpusFactAttribute : FactAttribute
    {
        public NifCorpusFactAttribute()
        {
            if (string.IsNullOrEmpty(Environment.GetEnvironmentVariable("FO4_NIF_DIR")))
                Skip = "Set FO4_NIF_DIR to run the vanilla collision corpus.";
        }
    }

    private static NifCollisionModel Decode(byte[] bytes)
    {
        Assert.True(NifCollisionDecoder.TryDecode(bytes, out var model, out var error), error);
        Assert.NotNull(model);
        return model!;
    }

    // Regression: a member-offset or unit change must surface as a body/material/shape mismatch, not a silent zero.
    [Fact]
    public void DecodesBodiesMaterialsAndShapes()
    {
        var model = Decode(NifCollisionFixture.Build().Bytes);

        Assert.Equal("hknpPhysicsSystemData", model.SystemClass);
        Assert.Equal(8, model.PointerSize);
        Assert.False(model.LengthPrefixed);

        Assert.Equal(3, model.Bodies.Count);
        Assert.Equal(0, model.Bodies[0].Shape);
        Assert.Equal(1, model.Bodies[1].Shape);
        Assert.Equal(3, model.Bodies[2].Shape);
        Assert.Equal(NifCollisionFixture.MaterialHash0, model.Bodies[0].MaterialHash);
        Assert.Equal(NifCollisionFixture.MaterialHash1, model.Bodies[1].MaterialHash);
        Assert.Equal(1, model.Bodies[0].Flags);
        Assert.Equal(new[] { 1.0f, 2.0f, 3.0f, 1.0f }, model.Bodies[0].Position);

        Assert.Equal(0.5f, model.Bodies[0].Response.DynamicFriction);
        Assert.Equal(0.25f, model.Bodies[0].Response.StaticFriction);
        Assert.Equal(0.125f, model.Bodies[0].Response.Restitution);
        Assert.Equal((byte)1, model.Bodies[0].Response.FrictionCombine);
        Assert.Equal((byte)2, model.Bodies[0].Response.RestitutionCombine);
        Assert.Equal(0.75f, model.Bodies[1].Response.DynamicFriction);

        Assert.Equal(4, model.Shapes.Count);
        Assert.Equal(NifCollisionShapeKind.Sphere, model.Shapes[0].Kind);
        Assert.Equal(NifCollisionFixture.SphereRadius, model.Shapes[0].ConvexRadius);

        var polytope = model.Shapes[2];
        Assert.Equal(NifCollisionShapeKind.ConvexPolytope, polytope.Kind);
        Assert.Equal(3, polytope.VertexCount);
        Assert.Equal(new[] { 0, 1, 2 }, polytope.Triangles);

        var compound = model.Shapes[1];
        Assert.Equal(NifCollisionShapeKind.Compound, compound.Kind);
        Assert.Equal(2, compound.Instances.Count);
        Assert.Equal(0, compound.Instances[0].Shape);
        Assert.Equal(2, compound.Instances[1].Shape);
        Assert.Equal(1.0f, compound.Instances[1].Transform[12]);

        var mesh = model.Shapes[3];
        Assert.Equal(NifCollisionShapeKind.CompressedMesh, mesh.Kind);
        Assert.Equal(4, mesh.VertexCount);
        Assert.Equal(new[] { 0, 1, 2, 1, 2, 3 }, mesh.Triangles);
        Assert.Equal(new[] { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 0.0f, 0.0f, 0.0f },
                     mesh.Vertices);
    }

    // Regression: a table offset change must fail here instead of silently moving the fixture with it.
    [Fact]
    public void TableOffsetsMatchTheMeasuredCollisionLayout()
    {
        Assert.Equal(64, CollisionPackfile.Member("hknpPhysicsSystemData", "bodyCinfos"));
        Assert.Equal(48, CollisionPackfile.Member("hknpBodyCinfo", "position"));
        Assert.Equal(64, CollisionPackfile.Member("hknpBodyCinfo", "orientation"));
        Assert.Equal(18, CollisionPackfile.Member("hknpBodyCinfo", "materialId"));
        Assert.Equal(18, CollisionPackfile.Member("hknpMaterial", "dynamicFriction"));
        Assert.Equal(22, CollisionPackfile.Member("hknpMaterial", "restitution"));
        Assert.Equal(20, CollisionPackfile.Member("hknpBSMaterial", "uiMaterialCRC"));
        Assert.Equal(48, CollisionPackfile.Member("hknpConvexPolytopeShape", "vertices"));
        Assert.Equal(68, CollisionPackfile.Member("hknpConvexPolytopeShape", "faces"));
        Assert.Equal(72, CollisionPackfile.Member("hknpConvexPolytopeShape", "indices"));
        Assert.Equal(96, CollisionPackfile.Member("hknpCompressedMeshShape", "data"));
        Assert.Equal(16, CollisionPackfile.Member("hknpCompressedMeshShapeData", "meshTree"));
        Assert.Equal(80, CollisionPackfile.Member("hkcdStaticMeshTreeBaseSection", "primitives"));
        Assert.Equal(88, CollisionPackfile.Member("hkcdStaticMeshTreeBaseSection", "numPackedVertices"));
        Assert.Equal(89, CollisionPackfile.Member("hkcdStaticMeshTreeBaseSection", "numSharedIndices"));
        Assert.Equal(92, CollisionPackfile.Member("hkcdStaticMeshTreeBaseSection", "page"));
    }

    // Regression: a dynamic compound must decode through the inherited instance array, not fall back to unknown.
    [Fact]
    public void DecodesDynamicCompoundInstances()
    {
        var model = Decode(NifCollisionFixture.BuildDynamicCompound().Bytes);

        Assert.Equal(4, model.Shapes.Count);
        Assert.Equal(NifCollisionShapeKind.Compound, model.Shapes[1].Kind);
        Assert.Equal("hknpDynamicCompoundShape", model.Shapes[1].Class);
        Assert.Equal(2, model.Shapes[1].Instances.Count);
        Assert.Equal(0, model.Shapes[1].Instances[0].Shape);
        Assert.Equal(2, model.Shapes[1].Instances[1].Shape);
    }

    // Regression: a free-list hole must be skipped instead of surfacing as an instance with no shape.
    [Fact]
    public void SkipsCompoundFreeListHoles()
    {
        var model = Decode(NifCollisionFixture.BuildWithFreeListHole().Bytes);

        Assert.Equal(2, model.Shapes[1].Instances.Count);
        Assert.Contains("note compound free-list holes skipped", model.Describe());
    }

    // Regression: the manifest must stay byte-stable across runs so corpus hashes compare.
    [Fact]
    public void DescribesTheSameBytesTheSameWay()
    {
        var bytes = NifCollisionFixture.Build().Bytes;
        var first = Decode(bytes);
        var second = Decode(bytes);

        Assert.Equal(first.Describe(), second.Describe());
        Assert.Equal(first.Sha256Hex(), second.Sha256Hex());

        var text = first.Describe();
        Assert.Contains("layout 8 bare hknpPhysicsSystemData", text);
        Assert.Contains("bodies 3", text);
        Assert.Contains("s 0 hknpSphereShape Sphere 0.75 0 0 0", text);
        Assert.Contains("s 3 hknpCompressedMeshShape CompressedMesh 0 4 2 0", text);
        Assert.DoesNotContain("note ", text);
    }

    // Regression: consuming the length prefix must not shift every member offset.
    [Fact]
    public void AcceptsAFourByteLengthPrefix()
    {
        var prefixed = Decode(NifCollisionFixture.Prefix(NifCollisionFixture.Build().Bytes));

        Assert.True(prefixed.LengthPrefixed);
        Assert.Equal(3, prefixed.Bodies.Count);
        Assert.Equal(4, prefixed.Shapes.Count);
    }

    // Regression: a non-packfile must fail closed instead of reading unrelated bytes.
    [Theory]
    [InlineData(0)]
    [InlineData(12)]
    [InlineData(64)]
    [InlineData(4096)]
    public void RefusesBlobsThatAreNotPackfiles(int length)
    {
        var bytes = new byte[length];
        Assert.False(NifCollisionDecoder.TryDecode(bytes, out var model, out var error));
        Assert.Null(model);
        Assert.NotEmpty(error);
    }

    // Regression: a renamed system class must be refused, not decoded at stale offsets.
    [Fact]
    public void RefusesAPackfileWithoutAPhysicsSystem()
    {
        var sample = NifCollisionFixture.Build();
        var bytes = (byte[])sample.Bytes.Clone();

        var names = System.Text.Encoding.ASCII.GetBytes("hknpPhysicsSystemData");
        int at = Find(bytes, names);
        Assert.True(at > 0, "the fixture no longer carries the system class name");
        bytes[at] = (byte)'x';

        Assert.False(NifCollisionDecoder.TryDecode(bytes, out _, out var error));
        Assert.Contains("no hknpPhysicsSystemData", error);
    }

    // Regression: a PSD revision with a different signature must be refused by name.
    [Fact]
    public void RefusesASystemWhoseSignatureDisagreesWithTheClassTable()
    {
        var bytes = NifCollisionFixture.Build().Bytes;
        int at = Find(bytes, System.Text.Encoding.ASCII.GetBytes("hknpPhysicsSystemData"));
        Assert.True(at > 5, "the fixture no longer carries the system class name");
        BitConverter.GetBytes(0xDEADBEEFu).CopyTo(bytes, at - 5);

        Assert.False(NifCollisionDecoder.TryDecode(bytes, out _, out var error));
        Assert.Contains("is signed 0xdeadbeef", error);
    }

    // Regression: a cut blob must be refused with an error or decode whole, never throw out of TryDecode.
    [Fact]
    public void NeverThrowsOnTruncatedBlobs()
    {
        var bytes = NifCollisionFixture.Build().Bytes;

        for (int length = 0; length < PackfileImage.HeaderSize; length++)
        {
            var cut = new byte[length];
            Array.Copy(bytes, cut, length);
            Assert.False(NifCollisionDecoder.TryDecode(cut, out _, out _));
        }

        for (int length = 0; length <= bytes.Length; length++)
        {
            var cut = new byte[length];
            Array.Copy(bytes, cut, length);
            if (!NifCollisionDecoder.TryDecode(cut, out _, out var error)) Assert.NotEmpty(error);
        }

        for (int tail = 0; tail < 64 && tail < bytes.Length; tail++)
        {
            var cut = new byte[bytes.Length - tail];
            Array.Copy(bytes, cut, cut.Length);
            if (!NifCollisionDecoder.TryDecode(cut, out _, out var error)) Assert.NotEmpty(error);
        }
    }

    // Regression: hostile array counts must be rejected before reading past the data section.
    [Fact]
    public void NeverThrowsOnHostileCountsAndRandomBytes()
    {
        var sample = NifCollisionFixture.Build();

        var patched = (byte[])sample.Bytes.Clone();
        int dataBase = BitConverter.ToInt32(patched, 0x40 + 0x14);
        BitConverter.GetBytes(int.MaxValue).CopyTo(patched, dataBase + sample.BodyHeader + 8);

        Assert.True(NifCollisionDecoder.TryDecode(patched, out var model, out var error), error);
        Assert.NotNull(model);
        Assert.Empty(model!.Bodies);

        var random = new Random(20261025);
        for (int run = 0; run < 200; run++)
        {
            var noisy = new byte[64 + random.Next(1024)];
            random.NextBytes(noisy);
            BitConverter.GetBytes(NifCollisionDecoder.Magic0).CopyTo(noisy, 0);
            NifCollisionDecoder.TryDecode(noisy, out _, out _);
        }
    }

    // Regression: a vanilla blob that stops decoding, or a changed hash, must fail the corpus manifest.
    [NifCorpusFact]
    public void VanillaNifCollisionBlobsDecodeToStableHashes()
    {
        var root = Environment.GetEnvironmentVariable("FO4_NIF_DIR")!;

        var lines = new List<string>();
        int blobs = 0;
        int failures = 0;

        foreach (var path in Directory.EnumerateFiles(root, "*.nif", SearchOption.AllDirectories))
        {
            NifFile nif;
            try
            {
                nif = NifFile.FromBytes(File.ReadAllBytes(path));
            }
            catch (Exception)
            {
                continue;
            }

            foreach (var collision in nif.Collisions)
            {
                if (collision.Bytes.Length == 0) continue;
                blobs++;

                if (!NifCollisionDecoder.TryDecode(collision.Bytes, out var model, out var error))
                {
                    failures++;
                    lines.Add($"{Path.GetRelativePath(root, path)}#{collision.Block} FAIL {error}");
                    continue;
                }

                lines.Add($"{Path.GetRelativePath(root, path)}#{collision.Block} {model!.SystemClass} " +
                          $"{model.Bodies.Count} {model.Shapes.Count} {model.Sha256Hex()}");
            }
        }

        lines.Sort(StringComparer.Ordinal);
        var manifest = string.Join("\n", lines) + "\n";
        File.WriteAllText(Path.Combine(AppContext.BaseDirectory, "nif-collision-manifest.txt"), manifest);

        var committed = Path.Combine(AppContext.BaseDirectory, "fixtures", "nif-collision", "manifest.txt");
        if (File.Exists(committed)) Assert.Equal(File.ReadAllText(committed), manifest);

        Assert.Equal(0, failures);
        Assert.True(blobs > 0, "FO4_NIF_DIR holds no NIF carrying collision");
    }

    private static int Find(byte[] haystack, byte[] needle)
    {
        for (int at = 0; at + needle.Length <= haystack.Length; at++)
        {
            bool same = true;
            for (int i = 0; i < needle.Length && same; i++) same = haystack[at + i] == needle[i];
            if (same) return at;
        }
        return -1;
    }
}
