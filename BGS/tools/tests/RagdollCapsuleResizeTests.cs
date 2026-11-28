using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using OpenCommonwealth.Services.Hkx;
using Xunit;
using Xunit.Abstractions;

namespace BehaviourStudio.Tests;

public sealed class RagdollCapsuleResizeTests
{
    private readonly ITestOutputHelper _out;

    public RagdollCapsuleResizeTests(ITestOutputHelper output) => _out = output;

    private static readonly byte[] Faces = Convert.FromHexString("0000040404000404080004040c00040410000404");

    private static readonly byte[] Indices = Convert.FromHexString("0706020303020001070504060100040501050703020604");

    private const string PVertices =
        "a7180f413ce239be3fb03b3c00afafbd05de39be67d03b3ca7180f41b3ae44be3fb03b3c00afafbd7caa44be67d03b3c" +
        "a7180f413ce239be908d6e3a00afafbd05de39be1090703aa7180f41b3ae44be908d6e3a";

    private const string PPlanes =
        "ab0eefb5ffff7fbf0000000085aa44bef2e9633579d4d4abfeff7f3f18d03bbcf2e963b579d4d42bfeff7fbf108b703a" +
        "ab0eef35ffff7f3f000000000fde393effff7fbfab0eef35f3e9633540afafbdffff7f3fab0eefb5f3e963b5a8180fc1" +
        "000000000000000000000000eeff7fff";

    private const string PProperties = "dd4603fd19008031040234773477003300800080ff7f30f50000a040e30f0541";

    private const string QVertices =
        "a8f71a42fb625040880b1c3e142dc740cd6350402c0b1c3ea8f71a4229dd3c40880b1c3e142dc740fbdd3c402c0b1c3e" +
        "a8f71a42fb6250408c511cbe142dc740cd635040e8511cbea8f71a4229dd3c408c511cbe";

    private const string QPlanes =
        "a2a0ceb5ffff7fbf0000000022de3c4031c636b32b869329ffff7f3f1a0b1cbe31c636332b8693a9ffff7fbffc511cbe" +
        "a2a0ce35ffff7f3f00000000f46350c0000080bfa3a0ce3532c636b3082dc7400000803fa3a0ceb532c63633a7f71ac2" +
        "000000000000000000000000eeff7fff";

    private const string QProperties = "ef596a0c00008032aa18be44be44003600800080ff7f30f50000a040d0751247";

    private static readonly Vector3 PA = new(-0.08051109f, -0.1867838f, 0.006190463f);

    private static readonly Vector3 PB = new(8.938247f, -0.18679987f, 0.0061828056f);

    private const float PRadius = 0.52200663f;

    private static readonly Vector3 QA = new(6.376774f, 3.1035697f, -0.00013492252f);

    private static readonly Vector3 QB = new(38.589333f, 3.1035202f, -0.0001335517f);

    private const float QRadius = 15.099609f;

    [Fact]
    public void AGoldenPairResizesToTheOtherCapsuleExactly()
    {
        RagdollCapsuleData from = Pair(PVertices, PPlanes, PProperties);
        RagdollCapsuleData wanted = Pair(QVertices, QPlanes, QProperties);

        RagdollCapsuleData derived = RagdollCapsule.Derive(from, PA, PB, PRadius, QA, QB, QRadius, "the shape",
                                                          "so nothing was changed");

        float scale = QRadius / 99f;
        float worst = 0f;
        for (int i = 0; i < RagdollCapsule.VertexCount; i++)
        {
            int at = i * RagdollCapsule.VertexWidth;
            Assert.Equal(RagdollCapsule.IndexTag + i, BitConverter.ToInt32(derived.Vertices, at + 12));
            for (int lane = 0; lane < 3; lane++)
            {
                int laneAt = at + lane * 4;
                float difference = Math.Abs(BitConverter.ToSingle(derived.Vertices, laneAt) -
                                            BitConverter.ToSingle(wanted.Vertices, laneAt));
                if (difference > worst) worst = difference;
                Assert.True(difference <= 2e-3f * scale,
                            $"vertex {i} lane {lane} is {difference} from the stored one, where 2e-3 of radius/99 " +
                            $"is {2e-3f * scale}");
            }
        }

        float worstPlane = 0f;
        for (int i = 0; i < RagdollCapsule.PlaneCount; i++)
        {
            int at = i * RagdollCapsule.PlaneWidth;
            for (int lane = 0; lane < 3; lane++)
            {
                int laneAt = at + lane * 4;
                Assert.Equal(BitConverter.ToInt32(from.Planes, laneAt), BitConverter.ToInt32(derived.Planes, laneAt));
                float normal = Math.Abs(BitConverter.ToSingle(derived.Planes, laneAt) -
                                        BitConverter.ToSingle(wanted.Planes, laneAt));
                Assert.True(normal <= 1e-4f, $"plane {i} lane {lane} normal is {normal} from the stored one");
            }

            if (i == RagdollCapsule.PlaneCount - 1) continue;

            float difference = Math.Abs(BitConverter.ToSingle(derived.Planes, at + 12) -
                                        BitConverter.ToSingle(wanted.Planes, at + 12));
            if (difference > worstPlane) worstPlane = difference;
            Assert.True(difference <= 2e-3f * scale,
                        $"plane {i} is {difference} from the stored d, where 2e-3 of radius/99 is {2e-3f * scale}");
        }

        Assert.Equal(Faces, derived.Faces);
        Assert.Equal(Indices, derived.Indices);
        Assert.Equal(wanted.MassProperties, derived.MassProperties);
        Assert.Equal(37493.8125f, BitConverter.ToSingle(derived.MassProperties!, 28));
        Assert.Equal(5f, BitConverter.ToSingle(derived.MassProperties!, 24));
        Assert.Equal((short)23023, BitConverter.ToInt16(derived.MassProperties!, 0));
        Assert.Equal((short)3178, BitConverter.ToInt16(derived.MassProperties!, 2));
        Assert.Equal((short)0, BitConverter.ToInt16(derived.MassProperties!, 4));
        Assert.Equal((short)12928, BitConverter.ToInt16(derived.MassProperties!, 6));
        Assert.Equal((short)6314, BitConverter.ToInt16(derived.MassProperties!, 8));
        Assert.Equal((short)17598, BitConverter.ToInt16(derived.MassProperties!, 10));
        Assert.Equal((short)17598, BitConverter.ToInt16(derived.MassProperties!, 12));
        Assert.Equal((short)13824, BitConverter.ToInt16(derived.MassProperties!, 14));
        _out.WriteLine($"the golden pair's vertices landed within {worst} ({worst / scale} of radius/99) and its " +
                       $"planes within {worstPlane} ({worstPlane / scale} of radius/99)");
    }

    [Fact]
    public void TheStoredMassAndMajorAxisSpaceSurviveAResize()
    {
        RagdollCapsuleData from = Pair(PVertices, PPlanes, PProperties);
        RagdollCapsuleData derived = RagdollCapsule.Derive(from, PA, PB, PRadius, QA, QB, QRadius, "the shape",
                                                          "so nothing was changed");

        Assert.Equal(5f, BitConverter.ToSingle(derived.MassProperties!, 24));
        for (int i = 16; i < 28; i++)
            Assert.Equal(from.MassProperties![i], derived.MassProperties![i]);

        Assert.NotEqual(BitConverter.ToSingle(from.MassProperties!, 28),
                        BitConverter.ToSingle(derived.MassProperties!, 28));
        Assert.NotEqual(BitConverter.ToInt32(from.MassProperties!, 8), BitConverter.ToInt32(derived.MassProperties!, 8));
        _out.WriteLine($"the resize kept the mass {BitConverter.ToSingle(derived.MassProperties!, 24)} and the " +
                       $"undecoded majorAxisSpace {Convert.ToHexString(derived.MassProperties!, 16, 8)}");
    }

    [Fact]
    public void EveryFixtureCapsuleScalesAndComesBack()
    {
        int shapes = 0;
        float worst = 0f;
        foreach (string file in Directory.GetFiles(FixturesRoot, "*.hkx", SearchOption.AllDirectories)
                                         .OrderBy(path => path, StringComparer.Ordinal))
        {
            byte[] bytes = File.ReadAllBytes(file);
            if (!CarriesRagdoll(bytes)) continue;

            var origin = new RagdollDocument(bytes);
            var document = new RagdollDocument(bytes);
            var stretched = new List<int>();
            for (int i = 0; i < document.Shapes.Count; i++)
            {
                var shape = document.Shapes[i];
                if (!shape.CarriesPolytopeData) continue;

                document.SetCapsuleEndpoints(i, shape.A, shape.A + (shape.B - shape.A) * 1.1f, true);
                stretched.Add(i);
            }

            if (stretched.Count == 0) continue;

            var back = new RagdollDocument(document.Serialize());
            foreach (int i in stretched)
            {
                var shape = back.Shapes[i];
                back.SetCapsuleEndpoints(i, shape.A, shape.A + (shape.B - shape.A) / 1.1f, true);
            }

            var again = new RagdollDocument(back.Serialize());
            Assert.Equal(origin.Shapes.Count, again.Shapes.Count);
            foreach (int i in stretched)
            {
                shapes++;
                var was = origin.Shapes[i].Capsule;
                var now = again.Shapes[i].Capsule;
                Assert.True(was is not null && now is not null,
                            $"{Path.GetFileName(file)} shape {i} lost its derived arrays");

                float scale = origin.Shapes[i].Radius / 99f;
                for (int vertex = 0; vertex < RagdollCapsule.VertexCount; vertex++)
                    for (int lane = 0; lane < 3; lane++)
                    {
                        int at = vertex * RagdollCapsule.VertexWidth + lane * 4;
                        float difference = Math.Abs(BitConverter.ToSingle(was!.Vertices, at) -
                                                    BitConverter.ToSingle(now!.Vertices, at));
                        if (difference > worst) worst = difference;
                        Assert.True(difference <= 2e-3f * scale,
                                    $"{Path.GetFileName(file)} shape {i} vertex {vertex} came back {difference} " +
                                    $"away where 2e-3 of radius/99 is {2e-3f * scale}");
                    }
            }
        }

        _out.WriteLine($"{shapes} capsule(s) scaled by 1.1 and back, worst vertex difference {worst}");
        Assert.True(shapes > 0, "no capsule shape was found in the fixtures");
    }

    [Fact]
    public void AResizeReadsBackAsTheDerivedPolytope()
    {
        var document = new RagdollDocument(Fixture());
        int index = 0;
        while (!document.Shapes[index].CarriesPolytopeData) index++;
        var shape = document.Shapes[index];
        byte[] before = shape.Capsule!.Vertices;
        float radius = shape.Radius;

        document.SetCapsule(index, shape.A, shape.A + (shape.B - shape.A) * 1.05f, radius * 1.2f, true);
        Assert.True(document.Dirty);

        var read = new RagdollDocument(document.Serialize());
        var edited = document.Shapes[index];
        Assert.True(edited.SameAs(read.Shapes[index]));
        Assert.NotEqual(before, read.Shapes[index].Capsule!.Vertices);
        Assert.Equal(radius * 1.2f, read.Shapes[index].Radius);
        Assert.True(RagdollCapsule.SameData(edited.Capsule, read.Shapes[index].Capsule));
        _out.WriteLine($"shape {index} resized to radius {read.Shapes[index].Radius} and read back with its " +
                       $"{read.Shapes[index].PolytopeCounts}");
    }

    [Fact]
    public void ADirectionChangeIsRefused()
    {
        var document = new RagdollDocument(Fixture());
        var shape = document.Shapes[0];
        Vector3 span = shape.B - shape.A;
        Vector3 axis = Math.Abs(span.X) < 0.9f * span.Length() ? Vector3.UnitX : Vector3.UnitY;
        Vector3 perpendicular = Vector3.Normalize(Vector3.Cross(span, axis));
        Vector3 bent = shape.B + perpendicular * (span.Length() * 0.01f);

        var swapped = Assert.Throws<InvalidOperationException>(
            () => document.SetCapsule(0, shape.B, shape.A, shape.Radius, true));
        Assert.Contains("direction", swapped.Message);
        Assert.Contains("direction", Assert.Throws<InvalidOperationException>(
            () => document.SetCapsuleEndpoints(0, shape.A, bent, true)).Message);
        Assert.False(document.Dirty);
    }

    [Fact]
    public void ANonPositiveRadiusOrNoLengthIsRefused()
    {
        var document = new RagdollDocument(Fixture());
        var shape = document.Shapes[0];

        Assert.Contains("positive finite", Assert.Throws<InvalidOperationException>(
            () => document.SetShapeRadius(0, 0f)).Message);
        Assert.Contains("positive finite", Assert.Throws<InvalidOperationException>(
            () => document.SetCapsule(0, shape.A, shape.B, float.NaN, true)).Message);
        Assert.Contains("not finite", Assert.Throws<InvalidOperationException>(
            () => document.SetCapsuleEndpoints(0, shape.A, new Vector3(float.PositiveInfinity, 0f, 0f),
                                              true)).Message);
        Assert.Contains("no length", Assert.Throws<InvalidOperationException>(
            () => document.SetCapsuleEndpoints(0, shape.A, shape.A, true)).Message);
        Assert.False(document.Dirty);
    }

    [Fact]
    public void ASharedShapeNeedsAllowShared()
    {
        var document = new RagdollDocument(Fixture());
        var shape = document.Shapes[0];
        float wanted = shape.Radius * 1.5f;
        shape.ReferencedByBodies = 3;

        var refused = Assert.Throws<InvalidOperationException>(() => document.SetShapeRadius(0, wanted));
        Assert.Contains("named by 3 body(ies)", refused.Message);
        Assert.False(document.Dirty);

        document.SetShapeRadius(0, wanted, true);
        Assert.True(document.Dirty);
        Assert.Equal(wanted, new RagdollDocument(document.Serialize()).Shapes[0].Radius);
    }

    [Fact]
    public void ADoctoredPolytopeIsRefusedByTheWriter()
    {
        byte[] bytes = Fixture();
        var document = new RagdollDocument(bytes);
        var original = document.Shapes[0];
        Assert.True(original.Capsule is not null);

        var stale = original.Copy();
        stale.Radius = original.Radius + 1f;
        Assert.Contains("does not produce", Assert.Throws<InvalidOperationException>(
            () => RagdollWriteback.Write(bytes, document.Ragdolls, document.Shapes,
                                         Array.Empty<RagdollChange>(),
                                         new[] { new RagdollShapeChange(stale, original) })).Message);

        var doctored = original.Copy();
        var indices = (byte[])original.Capsule!.Indices.Clone();
        indices[0] = 6;
        doctored.Capsule = new RagdollCapsuleData(original.Capsule.Vertices, original.Capsule.Planes,
                                                 original.Capsule.Faces, indices, original.Capsule.MassProperties);
        Assert.Contains("faces or indices", Assert.Throws<InvalidOperationException>(
            () => RagdollWriteback.Write(bytes, document.Ragdolls, document.Shapes,
                                         Array.Empty<RagdollChange>(),
                                         new[] { new RagdollShapeChange(doctored, original) })).Message);
    }

    [Fact]
    public void ADoctoredVertexTagOrPlaneIsRefused()
    {
        byte[] bytes = Fixture();
        var document = new RagdollDocument(bytes);
        var original = document.Shapes[0];

        var tagged = original.Copy();
        var vertices = (byte[])original.Capsule!.Vertices.Clone();
        BitConverter.GetBytes(0).CopyTo(vertices, 12);
        tagged.Capsule = new RagdollCapsuleData(vertices, original.Capsule.Planes, original.Capsule.Faces,
                                               original.Capsule.Indices, original.Capsule.MassProperties);
        var edited = tagged.Copy();
        edited.Radius = original.Radius + 1f;

        Assert.Contains("tags vertex 0", Assert.Throws<InvalidOperationException>(
            () => RagdollWriteback.Write(bytes, document.Ragdolls, document.Shapes,
                                         Array.Empty<RagdollChange>(),
                                         new[] { new RagdollShapeChange(edited, tagged) })).Message);

        var sentinel = original.Copy();
        var planes = (byte[])original.Capsule.Planes.Clone();
        BitConverter.GetBytes(0f).CopyTo(planes, (RagdollCapsule.PlaneCount - 1) * RagdollCapsule.PlaneWidth + 12);
        sentinel.Capsule = new RagdollCapsuleData(original.Capsule.Vertices, planes, original.Capsule.Faces,
                                                 original.Capsule.Indices, original.Capsule.MassProperties);
        var bent = sentinel.Copy();
        bent.Radius = original.Radius + 1f;

        Assert.Contains("sentinel", Assert.Throws<InvalidOperationException>(
            () => RagdollWriteback.Write(bytes, document.Ragdolls, document.Shapes,
                                         Array.Empty<RagdollChange>(),
                                         new[] { new RagdollShapeChange(bent, sentinel) })).Message);
    }

    [Fact]
    public void AShapeSharingAPropertiesContainerNeedsTheShapeFlag()
    {
        byte[] patched = RagdollSharedShapeFixture.ShareProperties(Fixture(), out int source, out int repointed);
        var document = new RagdollDocument(patched);
        int index = RagdollSharedShapeFixture.ShapeAt(document, source);

        Assert.NotEqual(source, repointed);
        Assert.Equal(2, document.Shapes[index].ShapesSharingProperties);
        Assert.Equal(2, document.Shapes[index].ShapesSharingMassProperties);
        Assert.Equal(2, document.Shapes[RagdollSharedShapeFixture.ShapeAt(document, repointed)].ShapesSharingProperties);
        Assert.True(document.Shapes[index].ReferencedByBodies <= 1);

        float wanted = document.Shapes[index].Radius * 1.5f;
        var refused = Assert.Throws<InvalidOperationException>(() => document.SetShapeRadius(index, wanted));
        Assert.Contains("shares its properties container", refused.Message);
        Assert.False(document.Dirty);

        document.SetShapeRadius(index, wanted, true);
        Assert.True(document.Dirty);
        Assert.Equal(wanted, document.Shapes[index].Radius);
        _out.WriteLine($"shape {index} shares its container with shape {RagdollSharedShapeFixture.ShapeAt(document, repointed)} and needed " +
                       $"--allow-shared-shape: {refused.Message}");
    }

    [Fact]
    public void ATieInTheOldDirectionKeepsItsSlotWhenTheNewDirectionBreaksIt()
    {
        Vector3 a = Vector3.Zero;
        Vector3 b = new(1f, 1f, 0f);
        const double mass = 4.0;
        const float radius = 0.1f;
        RagdollCapsuleData from = Tied(a, b, radius, mass);

        Vector3 uOld = Vector3.Normalize(b - a);
        Vector3 uNew = Vector3.Normalize(new Vector3(1f - 4e-7f, 1f, 0f));
        Assert.Equal(Math.Abs(uOld.X), Math.Abs(uOld.Y));
        Assert.True(Math.Abs(uNew.Y) > Math.Abs(uNew.X), "the new direction does not break the tie towards y");
        Assert.True(Vector3.Cross(uOld, uNew).Length() <= RagdollCapsule.DirectionTolerance);

        Vector3 bNew = uNew * (b - a).Length();
        RagdollCapsuleData derived = RagdollCapsule.Derive(from, a, b, radius, a, bNew, radius, "the shape",
                                                          "so nothing was changed");

        short laneX = BitConverter.ToInt16(derived.MassProperties!, 8);
        short laneY = BitConverter.ToInt16(derived.MassProperties!, 10);
        short laneZ = BitConverter.ToInt16(derived.MassProperties!, 12);
        Assert.Equal(laneY, laneZ);
        Assert.NotEqual(laneX, laneY);
        _out.WriteLine($"a tie on x kept the axial lane in x ({laneX}) while y and z hold the transverse " +
                       $"value ({laneY}) after the resize turned the direction towards y");
    }

    private static RagdollCapsuleData Tied(Vector3 a, Vector3 b, float radius, double mass)
    {
        Vector3 u = Vector3.Normalize(b - a);
        Vector3 e1 = Vector3.Normalize(Vector3.Cross(u, Math.Abs(u.X) < 0.9f ? Vector3.UnitX : Vector3.UnitY));
        Vector3 e2 = Vector3.Cross(u, e1);
        float scale = radius / (float)RagdollCapsule.VertexSpan;
        float wanted = scale * MathF.Sqrt(3f);

        var vertices = new byte[RagdollCapsule.VertexCount * RagdollCapsule.VertexWidth];
        for (int i = 0; i < RagdollCapsule.VertexCount; i++)
        {
            double angle = Math.PI * 2.0 * i / RagdollCapsule.VertexCount;
            Vector3 direction = e1 * (float)Math.Cos(angle) + e2 * (float)Math.Sin(angle);
            Vector3 vertex = (i < 3 ? a : b) + wanted * direction;
            int at = i * RagdollCapsule.VertexWidth;
            BitConverter.GetBytes(vertex.X).CopyTo(vertices, at);
            BitConverter.GetBytes(vertex.Y).CopyTo(vertices, at + 4);
            BitConverter.GetBytes(vertex.Z).CopyTo(vertices, at + 8);
            BitConverter.GetBytes(RagdollCapsule.IndexTag + i).CopyTo(vertices, at + 12);
        }

        var planes = new byte[RagdollCapsule.PlaneCount * RagdollCapsule.PlaneWidth];
        for (int i = 0; i < RagdollCapsule.PlaneCount - 1; i++)
        {
            double angle = Math.PI * 2.0 * i / 6.0 + 0.3;
            Vector3 normal = Vector3.Normalize(e1 * (float)Math.Cos(angle) + e2 * (float)Math.Sin(angle));
            float support = float.NegativeInfinity;
            for (int v = 0; v < RagdollCapsule.VertexCount; v++)
            {
                int at = v * RagdollCapsule.VertexWidth;
                Vector3 vertex = new(BitConverter.ToSingle(vertices, at), BitConverter.ToSingle(vertices, at + 4),
                                     BitConverter.ToSingle(vertices, at + 8));
                support = Math.Max(support, Vector3.Dot(normal, vertex));
            }

            int planeAt = i * RagdollCapsule.PlaneWidth;
            BitConverter.GetBytes(normal.X).CopyTo(planes, planeAt);
            BitConverter.GetBytes(normal.Y).CopyTo(planes, planeAt + 4);
            BitConverter.GetBytes(normal.Z).CopyTo(planes, planeAt + 8);
            BitConverter.GetBytes(-support).CopyTo(planes, planeAt + 12);
        }

        BitConverter.GetBytes(float.MinValue)
                     .CopyTo(planes, (RagdollCapsule.PlaneCount - 1) * RagdollCapsule.PlaneWidth + 12);

        RagdollCapsule.EncodedMass encoded = RagdollCapsule.Encode(mass, a, b, radius, 0, "the shape",
                                                                  "so nothing was changed");
        var properties = new byte[RagdollCapsule.MassPropertiesWidth];
        for (int i = 0; i < 4; i++) BitConverter.GetBytes((short)encoded.Centre[i]).CopyTo(properties, i * 2);
        for (int i = 0; i < 4; i++) BitConverter.GetBytes((short)encoded.Inertia[i]).CopyTo(properties, 8 + i * 2);
        BitConverter.GetBytes((float)mass).CopyTo(properties, 24);
        BitConverter.GetBytes((float)encoded.Volume).CopyTo(properties, 28);

        return new RagdollCapsuleData(vertices, planes, Faces, Indices, properties);
    }
    private static RagdollCapsuleData Pair(string vertices, string planes, string properties) =>
        new(Vertices(vertices), Convert.FromHexString(planes), Faces, Indices, Convert.FromHexString(properties));

    private static RagdollCapsuleData Pair(string vertices, string planes, byte[] properties) =>
        new(Vertices(vertices), Convert.FromHexString(planes), Faces, Indices, properties);

    private static byte[] Vertices(string hex)
    {
        byte[] raw = Convert.FromHexString(hex);
        var payload = new byte[RagdollCapsule.VertexCount * RagdollCapsule.VertexWidth];
        for (int i = 0; i < RagdollCapsule.VertexCount; i++)
        {
            raw.AsSpan(i * 12, 12).CopyTo(payload.AsSpan(i * RagdollCapsule.VertexWidth));
            BitConverter.GetBytes(RagdollCapsule.IndexTag + i).CopyTo(payload, i * RagdollCapsule.VertexWidth + 12);
        }

        return payload;
    }

    private static double Factor(short word) => 3.0 * Math.Pow(2.0, (14080 - word) / 128.0);

    private static byte[] Fixture() => File.ReadAllBytes(Path.Combine(
        FixturesRoot, "Meshes", "Actors", "Character", "CharacterAssets", "skeleton.hkx"));

    private static string FixturesRoot => Path.Combine(AppContext.BaseDirectory, "fixtures", "vanilla");

    private static bool CarriesRagdoll(byte[] bytes)
    {
        try
        {
            return new PackfileObjects(PackfileImage.Read(bytes)).Instances
                .Any(instance => instance.ClassName == "hknpRagdollData");
        }
        catch (Exception)
        {
            return false;
        }
    }
}
