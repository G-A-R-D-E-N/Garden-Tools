using System;
using System.Numerics;

namespace OpenCommonwealth.Services.Hkx;

internal sealed record RagdollCapsuleData(byte[] Vertices, byte[] Planes, byte[] Faces, byte[] Indices,
                                         byte[]? MassProperties);

internal static class RagdollCapsule
{
    internal const int VertexCount = 7;
    internal const int PlaneCount = 7;
    internal const int FaceCount = 5;
    internal const int IndexCount = 23;
    internal const int VertexWidth = 16;
    internal const int PlaneWidth = 16;
    internal const int FaceWidth = 4;
    internal const int MassPropertiesWidth = 32;
    internal const int IndexTag = 0x3F000000;
    internal const int QuantizedLimit = 32767;
    internal const int CentreWordBase = 14208;
    internal const int InertiaWordBase = 14080;
    internal const int ExponentWordStep = 128;
    internal const int ZeroCentreExponent = 60;
    internal const int CentreTolerance = 1;
    internal const double CentreLimit = 1e-6;
    internal const int InertiaTolerance = 1;
    internal const float DirectionTolerance = 1e-6f;
    internal const float VertexScaleTolerance = 1e-3f;
    internal const float PlaneTolerance = 5e-4f;
    internal const float NormalTolerance = 1e-3f;
    internal const float VolumeTolerance = 1e-4f;
    internal const float SentinelLimit = -1e38f;
    internal const double VertexSpan = 99.0;
    internal const int Guard = 512;

    private static readonly byte[] FacePayload =
    {
        0, 0, 4, 4, 4, 0, 4, 4, 8, 0, 4, 4, 12, 0, 4, 4, 16, 0, 4, 4,
    };

    private static readonly byte[] IndexPayload =
    {
        7, 6, 2, 3, 3, 2, 0, 1, 7, 5, 4, 6, 1, 0, 4, 5, 1, 5, 7, 3, 2, 6, 4,
    };

    internal sealed record EncodedMass(double Volume, int CentreExponent, int[] Centre, int InertiaExponent,
                                      int[] Inertia);

    internal static RagdollCapsuleData Derive(RagdollCapsuleData old, Vector3 aOld, Vector3 bOld, float radiusOld,
                                             Vector3 aNew, Vector3 bNew, float radiusNew, string what, string tail)
    {
        ArgumentNullException.ThrowIfNull(old);
        if (!RagdollValues.Finite(aNew) || !RagdollValues.Finite(bNew))
            throw Refuse(what, tail, "would be given non-finite endpoints");
        if (!float.IsFinite(radiusNew) || radiusNew <= 0f)
            throw Refuse(what, tail, $"would be given a radius of {radiusNew} that is not a positive finite number");

        float span = (bNew - aNew).Length();
        if (!(span > 0f) || !float.IsFinite(span))
            throw Refuse(what, tail, "would be given endpoints with no length between them");

        bool[] atB = RequireGeometry(old, aOld, bOld, radiusOld, what, tail);
        RequireMassProperties(old, aOld, bOld, radiusOld, what, tail);

        Vector3 uOld = Unit(bOld - aOld, what, tail);
        int dominant = Dominant(uOld);
        Vector3 uNew = Unit(bNew - aNew, what, tail);
        if (Vector3.Dot(uOld, uNew) <= 0f || Vector3.Cross(uOld, uNew).Length() > DirectionTolerance)
            throw Refuse(what, tail,
                         "would change the capsule's direction, and this build resizes a capsule only while its " +
                         "direction stays the same");

        float scale = radiusNew / (float)VertexSpan / (radiusOld / (float)VertexSpan);

        var vertices = new byte[old.Vertices.Length];
        var moved = new Vector3[VertexCount];
        for (int i = 0; i < VertexCount; i++)
        {
            Vector3 vertex = ReadVector(old.Vertices, i * VertexWidth);
            Vector3 from = atB[i] ? bOld : aOld;
            Vector3 to = atB[i] ? bNew : aNew;
            moved[i] = to + scale * (vertex - from);
            WriteVector(vertices, i * VertexWidth, moved[i]);
            old.Vertices.AsSpan(i * VertexWidth + 12, 4).CopyTo(vertices.AsSpan(i * VertexWidth + 12));
        }

        var planes = (byte[])old.Planes.Clone();
        for (int i = 0; i < PlaneCount - 1; i++)
        {
            Vector3 normal = ReadVector(planes, i * PlaneWidth);
            double support = double.NegativeInfinity;
            for (int v = 0; v < VertexCount; v++)
            {
                double reach = (double)normal.X * moved[v].X + (double)normal.Y * moved[v].Y +
                               (double)normal.Z * moved[v].Z;
                if (reach > support) support = reach;
            }

            if (!double.IsFinite(support))
                throw Refuse(what, tail, $"cannot place plane {i} against the derived vertices");
            BitConverter.GetBytes((float)-support).CopyTo(planes, i * PlaneWidth + 12);
        }

        byte[]? mass = null;
        if (old.MassProperties is { } stored)
            mass = DeriveMassProperties(stored, BitConverter.ToSingle(stored, 24), aNew, bNew, radiusNew, dominant,
                                        what, tail);

        return new RagdollCapsuleData(vertices, planes, (byte[])old.Faces.Clone(), (byte[])old.Indices.Clone(),
                                      mass);
    }

    internal static bool SameData(RagdollCapsuleData? left, RagdollCapsuleData? right)
    {
        if (ReferenceEquals(left, right)) return true;
        if (left is null || right is null) return false;

        return SameBytes(left.Vertices, right.Vertices) && SameBytes(left.Planes, right.Planes) &&
               SameBytes(left.Faces, right.Faces) && SameBytes(left.Indices, right.Indices) &&
               SameBytes(left.MassProperties, right.MassProperties);
    }

    internal static bool SameBytes(byte[]? left, byte[]? right)
    {
        if (ReferenceEquals(left, right)) return true;
        if (left is null || right is null || left.Length != right.Length) return false;
        return left.AsSpan().SequenceEqual(right);
    }

    internal readonly record struct MassPropertiesSite(int At, int PropertiesIndex, int MassPropertiesIndex);

    internal static MassPropertiesSite? MassPropertiesSiteAt(PackfileObjects objects, PackfileObjects.Instance shape)
    {
        ArgumentNullException.ThrowIfNull(objects);
        if (objects.FieldAt(shape, "properties") is not int propertiesAt) return null;

        var container = objects.ReadRefAt(propertiesAt, out bool wasNull);
        if (wasNull) return null;
        if (container == null || container.ClassName != RagdollLayout.RefCountedPropertiesClass)
            throw new InvalidDataException(
                $"{shape.ClassName} at 0x{shape.Offset:x} points its properties at " +
                $"{container?.ClassName ?? "nothing"} where this build reads " +
                $"{RagdollLayout.RefCountedPropertiesClass}");

        if (objects.FieldAt(container, "entries") is not int entriesAt ||
            objects.ArrayAt(entriesAt, RagdollLayout.Stride(RagdollLayout.RefCountedPropertiesEntryClass))
                is not { } entries || entries.Count != 1)
            throw new InvalidDataException(
                $"{shape.ClassName} at 0x{shape.Offset:x} carries a properties container this build cannot read");

        var target = objects.ReadRefAt(entries.At, out bool entryWasNull);
        if (entryWasNull || target == null || target.ClassName != RagdollLayout.MassPropertiesClass)
            throw new InvalidDataException(
                $"{shape.ClassName} at 0x{shape.Offset:x} names " +
                $"{target?.ClassName ?? "nothing"} where this build reads {RagdollLayout.MassPropertiesClass}");

        return new MassPropertiesSite(
            target.Offset + RagdollLayout.Field(RagdollLayout.MassPropertiesClass, "compressedMassProperties"),
            objects.IndexOf(container), objects.IndexOf(target));
    }

    internal static int? MassPropertiesAt(PackfileObjects objects, PackfileObjects.Instance shape) =>
        MassPropertiesSiteAt(objects, shape)?.At;

    private static bool[] RequireGeometry(RagdollCapsuleData data, Vector3 a, Vector3 b, float radius,
                                          string what, string tail)
    {
        if (!RagdollValues.Finite(a) || !RagdollValues.Finite(b))
            throw Refuse(what, tail, "has non-finite endpoints");
        if (!float.IsFinite(radius) || radius <= 0f)
            throw Refuse(what, tail, $"stores a radius of {radius} that is not a positive finite number");
        if (data.Vertices.Length != VertexCount * VertexWidth || data.Planes.Length != PlaneCount * PlaneWidth ||
            data.Faces.Length != FaceCount * FaceWidth || data.Indices.Length != IndexCount)
            throw Refuse(what, tail,
                         $"carries {data.Vertices.Length / VertexWidth} vertices, " +
                         $"{data.Planes.Length / PlaneWidth} planes, {data.Faces.Length / FaceWidth} faces and " +
                         $"{data.Indices.Length} indices where a capsule stores {VertexCount}, {PlaneCount}, " +
                         $"{FaceCount} and {IndexCount}");

        float span = (b - a).Length();
        if (!(span > 0f) || !float.IsFinite(span))
            throw Refuse(what, tail, "has no length between its endpoints");

        float scale = radius / (float)VertexSpan;
        if (!(scale > 0f) || !float.IsFinite(scale))
            throw Refuse(what, tail, "has a radius this build cannot scale its vertices by");

        double wanted = scale * Math.Sqrt(3.0);
        var atB = new bool[VertexCount];
        int onA = 0, onB = 0;
        for (int i = 0; i < VertexCount; i++)
        {
            int tag = BitConverter.ToInt32(data.Vertices, i * VertexWidth + 12);
            if (tag != IndexTag + i)
                throw Refuse(what, tail,
                             $"tags vertex {i} with 0x{tag:x8} where a capsule tags it 0x{IndexTag + i:x8}");

            Vector3 vertex = ReadVector(data.Vertices, i * VertexWidth);
            double toA = (vertex - a).Length();
            double toB = (vertex - b).Length();
            bool nearestB = toB < toA;
            atB[i] = nearestB;
            if (nearestB) onB++;
            else onA++;

            double distance = nearestB ? toB : toA;
            if (Math.Abs(distance - wanted) > VertexScaleTolerance * scale)
                throw Refuse(what, tail,
                             $"places vertex {i} {distance} from its nearest endpoint where radius/99 * sqrt(3) " +
                             $"is {wanted}");
        }

        if (onA != 3 || onB != 4)
            throw Refuse(what, tail, $"splits {onA} vertices to a and {onB} to b where a capsule splits 3 and 4");

        for (int i = 0; i < PlaneCount - 1; i++)
        {
            Vector3 normal = ReadVector(data.Planes, i * PlaneWidth);
            float length = normal.Length();
            if (Math.Abs(length - 1f) > NormalTolerance)
                throw Refuse(what, tail, $"stores plane {i} with a normal of length {length}");

            float storedD = BitConverter.ToSingle(data.Planes, i * PlaneWidth + 12);
            double support = double.NegativeInfinity;
            for (int v = 0; v < VertexCount; v++)
            {
                Vector3 vertex = ReadVector(data.Vertices, v * VertexWidth);
                double reach = (double)normal.X * vertex.X + (double)normal.Y * vertex.Y +
                               (double)normal.Z * vertex.Z;
                if (reach > support) support = reach;
            }

            if (!double.IsFinite(support) || Math.Abs(storedD - -(float)support) > PlaneTolerance)
                throw Refuse(what, tail,
                             $"stores plane {i} at d {storedD} where its vertices support {-support}");
        }

        Vector3 sentinel = ReadVector(data.Planes, (PlaneCount - 1) * PlaneWidth);
        float sentinelD = BitConverter.ToSingle(data.Planes, (PlaneCount - 1) * PlaneWidth + 12);
        if (sentinel != Vector3.Zero || !(sentinelD <= SentinelLimit))
            throw Refuse(what, tail, "has no sentinel in its last plane");

        for (int i = 0; i < FaceCount; i++)
        {
            int at = i * FaceWidth;
            if (data.Faces[at] != FacePayload[at] || data.Faces[at + 1] != FacePayload[at + 1] ||
                data.Faces[at + 2] != FacePayload[at + 2] ||
                (data.Faces[at + 3] != 4 && data.Faces[at + 3] != 128))
                throw Refuse(what, tail, $"carries an unexpected face payload at face {i}");
        }

        if (!data.Indices.AsSpan().SequenceEqual(IndexPayload))
            throw Refuse(what, tail, "carries an unexpected index payload");

        return atB;
    }

    private static void RequireMassProperties(RagdollCapsuleData data, Vector3 a, Vector3 b, float radius,
                                              string what, string tail)
    {
        if (data.MassProperties is not { } stored) return;
        if (stored.Length != MassPropertiesWidth)
            throw Refuse(what, tail,
                         $"stores {stored.Length} mass property bytes where {MassPropertiesWidth} are expected");

        double mass = BitConverter.ToSingle(stored, 24);
        if (!(mass > 0.0) || !double.IsFinite(mass))
            throw Refuse(what, tail, $"stores a mass of {mass} that cannot scale an inertia");

        EncodedMass encoded = Encode(mass, a, b, radius, Dominant(Unit(b - a, what, tail)), what, tail);

        float storedVolume = BitConverter.ToSingle(stored, 28);
        if (!(storedVolume > 0f) || Math.Abs(storedVolume - encoded.Volume) > VolumeTolerance * encoded.Volume)
            throw Refuse(what, tail,
                         $"stores a volume of {storedVolume} where its endpoints and radius give {encoded.Volume}");

        int centreExponent = StoredExponent(stored, 6, CentreWordBase, "centre of mass", what, tail);
        if (centreExponent != encoded.CentreExponent)
            throw Refuse(what, tail,
                         $"stores centre of mass exponent {centreExponent} where its endpoints give " +
                         $"{encoded.CentreExponent}");
        double lsb = Math.Pow(2.0, -centreExponent);
        double[] mid = { ((double)a.X + b.X) * 0.5, ((double)a.Y + b.Y) * 0.5, ((double)a.Z + b.Z) * 0.5 };
        for (int i = 0; i < 3; i++)
        {
            double reconstructed = BitConverter.ToInt16(stored, i * 2) * lsb;
            bool close = Math.Abs(reconstructed - mid[i]) <= CentreLimit ||
                         Math.Abs(BitConverter.ToInt16(stored, i * 2) - encoded.Centre[i]) <= CentreTolerance;
            if (!close)
                throw Refuse(what, tail,
                             $"stores centre of mass lane {i} where its endpoints give {encoded.Centre[i]}");
        }

        int inertiaExponent = StoredExponent(stored, 14, InertiaWordBase, "inertia", what, tail);
        if (inertiaExponent != encoded.InertiaExponent)
            throw Refuse(what, tail,
                         $"stores inertia exponent {inertiaExponent} where its geometry gives " +
                         $"{encoded.InertiaExponent}");
        for (int i = 0; i < 3; i++)
            if (Math.Abs(BitConverter.ToInt16(stored, 8 + i * 2) - encoded.Inertia[i]) > InertiaTolerance)
                throw Refuse(what, tail,
                             $"stores inertia lane {i} where its geometry gives {encoded.Inertia[i]}");
    }

    private static byte[] DeriveMassProperties(byte[] stored, float mass, Vector3 a, Vector3 b, float radius,
                                               int dominant, string what, string tail)
    {
        EncodedMass encoded = Encode(mass, a, b, radius, dominant, what, tail);
        var produced = (byte[])stored.Clone();
        for (int i = 0; i < 4; i++) BitConverter.GetBytes((short)encoded.Centre[i]).CopyTo(produced, i * 2);
        for (int i = 0; i < 4; i++) BitConverter.GetBytes((short)encoded.Inertia[i]).CopyTo(produced, 8 + i * 2);
        BitConverter.GetBytes((float)encoded.Volume).CopyTo(produced, 28);
        return produced;
    }

    internal static EncodedMass Encode(double mass, Vector3 a, Vector3 b, float radius, int dominant,
                                      string what, string tail)
    {
        double length = (b - a).Length();
        double r = radius;
        double volume = Math.PI * r * r * length + 4.0 / 3.0 * Math.PI * r * r * r;
        if (!(volume > 0.0) || !double.IsFinite(volume))
            throw Refuse(what, tail, "has no volume to compute an inertia from");

        double midX = ((double)a.X + b.X) * 0.5;
        double midY = ((double)a.Y + b.Y) * 0.5;
        double midZ = ((double)a.Z + b.Z) * 0.5;
        double reach = Math.Max(Math.Abs(midX), Math.Max(Math.Abs(midY), Math.Abs(midZ)));

        int centreExponent = CentreExponent(reach, what, tail);
        var centre = new int[4];
        if (reach > 0.0)
        {
            double factor = Math.Pow(2.0, centreExponent);
            centre[0] = Quantize(midX * factor);
            centre[1] = Quantize(midY * factor);
            centre[2] = Quantize(midZ * factor);
        }

        centre[3] = Word(CentreWordBase, centreExponent, what, tail, "centre of mass");

        double cylinder = Math.PI * r * r * length;
        double hemisphere = 2.0 / 3.0 * Math.PI * r * r * r;
        double axialUnit = 0.5 * cylinder * r * r + 2.0 * (2.0 / 5.0) * hemisphere * r * r;
        double arm = length / 2.0 + 3.0 * r / 8.0;
        double transverseUnit = cylinder * (3.0 * r * r + length * length) / 12.0 +
                                2.0 * (83.0 / 320.0 * hemisphere * r * r + hemisphere * arm * arm);
        double density = mass / volume;
        double axial = density * axialUnit;
        double transverse = density * transverseUnit;
        double largest = Math.Max(axial, transverse);
        if (!(largest > 0.0) || !double.IsFinite(largest))
            throw Refuse(what, tail, "has no inertia to store");

        int inertiaExponent = InertiaExponent(largest, what, tail);
        double factor3 = 3.0 * Math.Pow(2.0, inertiaExponent);
        var inertia = new int[4];
        for (int i = 0; i < 3; i++) inertia[i] = Quantize(factor3 * (i == dominant ? axial : transverse));
        inertia[3] = Word(InertiaWordBase, inertiaExponent, what, tail, "inertia");

        return new EncodedMass(volume, centreExponent, centre, inertiaExponent, inertia);
    }

    private static int CentreExponent(double reach, string what, string tail)
    {
        if (!(reach > 0.0)) return ZeroCentreExponent;

        int exponent = (int)Math.Floor(Math.Log2(QuantizedLimit / reach));
        int steps = 0;
        while (reach * Math.Pow(2.0, exponent) > QuantizedLimit && steps++ < Guard) exponent--;
        while (reach * Math.Pow(2.0, exponent + 1) <= QuantizedLimit && steps++ < Guard) exponent++;
        if (steps >= Guard || reach * Math.Pow(2.0, exponent) > QuantizedLimit)
            throw Refuse(what, tail, "cannot encode its centre of mass");

        return exponent;
    }

    private static int InertiaExponent(double largest, string what, string tail)
    {
        int exponent = (int)Math.Floor(Math.Log2(QuantizedLimit / (3.0 * largest)));
        int steps = 0;
        while (3.0 * largest * Math.Pow(2.0, exponent) > QuantizedLimit && steps++ < Guard) exponent--;
        while (3.0 * largest * Math.Pow(2.0, exponent + 1) <= QuantizedLimit && steps++ < Guard) exponent++;
        if (steps >= Guard || 3.0 * largest * Math.Pow(2.0, exponent) > QuantizedLimit)
            throw Refuse(what, tail, "cannot encode its inertia");

        return exponent;
    }

    private static int Word(int wordBase, int exponent, string what, string tail, string field)
    {
        int word = wordBase - ExponentWordStep * exponent;
        if (word < short.MinValue || word > short.MaxValue)
            throw Refuse(what, tail, $"cannot encode its {field} exponent {exponent}");
        return word;
    }

    private static int StoredExponent(byte[] stored, int at, int wordBase, string field, string what, string tail)
    {
        int word = BitConverter.ToInt16(stored, at);
        if ((wordBase - word) % ExponentWordStep != 0)
            throw Refuse(what, tail, $"stores a {field} word of {word} that does not name an exponent");
        return (wordBase - word) / ExponentWordStep;
    }

    private static int Dominant(Vector3 u)
    {
        float x = Math.Abs(u.X), y = Math.Abs(u.Y), z = Math.Abs(u.Z);
        if (x >= y && x >= z) return 0;
        return y >= z ? 1 : 2;
    }

    private static int Quantize(double value)
    {
        double rounded = Math.Round(value, MidpointRounding.AwayFromZero);
        if (rounded >= short.MaxValue) return short.MaxValue;
        if (rounded <= short.MinValue) return short.MinValue;
        return (int)rounded;
    }

    private static Vector3 Unit(Vector3 span, string what, string tail)
    {
        float length = span.Length();
        if (!(length > 0f) || !float.IsFinite(length))
            throw Refuse(what, tail, "has no direction between its endpoints");
        return span / length;
    }

    private static Vector3 ReadVector(byte[] payload, int at) =>
        new(BitConverter.ToSingle(payload, at), BitConverter.ToSingle(payload, at + 4),
            BitConverter.ToSingle(payload, at + 8));

    private static void WriteVector(byte[] payload, int at, Vector3 value)
    {
        BitConverter.GetBytes(value.X).CopyTo(payload, at);
        BitConverter.GetBytes(value.Y).CopyTo(payload, at + 4);
        BitConverter.GetBytes(value.Z).CopyTo(payload, at + 8);
    }

    private static InvalidOperationException Refuse(string what, string tail, string reason) =>
        new($"{what} {reason}, {tail}");
}
