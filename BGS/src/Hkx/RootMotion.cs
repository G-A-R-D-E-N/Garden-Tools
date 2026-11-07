using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using OpenCommonwealth.Services;

namespace OpenCommonwealth.Services.Hkx;

public static partial class RootMotion
{

    public sealed record Sample(Vector3 Position, float TurnRadians)
    {
        public override string ToString() =>
            $"{Position.X:F2} {Position.Y:F2} {Position.Z:F2}, turned {TurnRadians * 180 / MathF.PI:F1} degrees";
    }

    public sealed class Motion
    {

        public Vector3 Up = Vector3.UnitZ;
        public Vector3 Forward = Vector3.UnitY;

        public float Duration;
        public string? UnsupportedReason;
        public readonly List<Sample> Samples = new();

        public const float MaximumPreviewTravel = 1_000_000;

        public bool Any => UnsupportedReason == null && Samples.Count > 0;

        public bool LooksInvalid => Any && Invalid;

        public bool Previewable => Any && !Invalid;

        private static bool Finite(Vector3 value) =>
            float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);

        private bool _invalid;
        private float _distance;
        private int _measuredSamples = -1;
        private float _measuredDuration = float.NaN;
        private string? _measuredReason;

        private void Measure()
        {
            if (_measuredSamples == Samples.Count && _measuredDuration.Equals(Duration) &&
                string.Equals(_measuredReason, UnsupportedReason, StringComparison.Ordinal))
                return;

            bool finite = float.IsFinite(Duration) && Duration >= 0 && Finite(Up) && Finite(Forward);
            float distance = 0;
            for (int i = 0; i < Samples.Count; i++)
            {
                var sample = Samples[i];
                if (!Finite(sample.Position) || !float.IsFinite(sample.TurnRadians)) finite = false;
                if (i > 0) distance += Vector3.Distance(Samples[i - 1].Position, sample.Position);
            }

            _distance = distance;
            _invalid = !finite || !float.IsFinite(distance) || distance > MaximumPreviewTravel;
            _measuredSamples = Samples.Count;
            _measuredDuration = Duration;
            _measuredReason = UnsupportedReason;
        }

        private bool Invalid { get { Measure(); return _invalid; } }

        public float Distance { get { Measure(); return _distance; } }

        public Vector3 Travel => Samples.Count > 1 ? Samples[^1].Position - Samples[0].Position
                                                   : Vector3.Zero;

        public float Turn => Samples.Count > 1 ? Samples[^1].TurnRadians - Samples[0].TurnRadians : 0;

        public override string ToString() =>
            UnsupportedReason != null ? "root motion kind not supported: " + UnsupportedReason
            : LooksInvalid ? $"root motion looks invalid ({Distance:F1} units)"
            : !Any ? "no root motion"
                 : $"{Samples.Count} samples over {Duration:F2}s, travelling {Distance:F1} units " +
                   $"and turning {Turn * 180 / MathF.PI:F0} degrees";
    }

    public static Motion Read(byte[] hkx, HavokClassTypes? types = null)
    {
        types ??= HavokClassTypes.Shipped;

        var motion = new Motion();
        var image = PackfileImage.Read(hkx);
        var objects = new PackfileObjects(image, types: types);

        var animation = objects.Instances.FirstOrDefault(i => objects.FieldAt(i, "extractedMotion") != null);
        var frame = animation == null
            ? objects.Instances.FirstOrDefault(i => i.ClassName.Contains("ReferenceFrame", StringComparison.Ordinal))
            : objects.ReadRef(animation, "extractedMotion", out _);
        if (frame == null) return motion;
        int kind = objects.ReadNarrowAt(frame.Offset + (image.Layout.PointerSize == 8 ? 16 : 8), 1) ?? -1;
        if (frame.ClassName != "hkaDefaultAnimatedReferenceFrame" || kind is not (0 or 1))
        {
            motion.UnsupportedReason = $"{frame.ClassName}, frameType {kind}";
            return motion;
        }

        Vector3 Vector(string field)
        {
            var read = objects.ReadFloats(frame, field, 3);
            return read == null ? Vector3.Zero : new Vector3(read[0], read[1], read[2]);
        }

        var up = Vector("up");
        var forward = Vector("forward");
        if (up != Vector3.Zero) motion.Up = up;
        if (forward != Vector3.Zero) motion.Forward = forward;

        motion.Duration = objects.ReadFloat(frame, "duration") ?? 0;

        var samples = objects.ReadValueArray(frame, "referenceFrameSamples", 16,
            (bytes, at) => new Sample(
                new Vector3(BitConverter.ToSingle(bytes, at),
                            BitConverter.ToSingle(bytes, at + 4),
                            BitConverter.ToSingle(bytes, at + 8)),
                BitConverter.ToSingle(bytes, at + 12)));

        if (samples != null) motion.Samples.AddRange(samples);
        return motion;
    }

    public static Motion Read(string path, HavokClassTypes? types = null) =>
        Read(InputFilePolicy.ReadHkx(path), types);

    public static Sample At(Motion motion, float fraction)
    {
        if (!motion.Previewable) return new Sample(Vector3.Zero, 0);
        if (motion.Samples.Count == 1) return motion.Samples[0];

        float where = Math.Clamp(fraction, 0, 1) * (motion.Samples.Count - 1);
        int first = (int)where;
        int second = Math.Min(first + 1, motion.Samples.Count - 1);
        float between = where - first;

        var a = motion.Samples[first];
        var b = motion.Samples[second];

        return new Sample(Vector3.Lerp(a.Position, b.Position, between),
                          a.TurnRadians + (b.TurnRadians - a.TurnRadians) * between);
    }
}
