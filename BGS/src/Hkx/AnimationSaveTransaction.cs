using System;
using System.IO;
using System.Linq;
using System.Numerics;

namespace OpenCommonwealth.Services.Hkx;

internal static class AnimationSaveTransaction
{
    internal sealed record Result(
        bool Committed,
        bool Spline,
        NativeAnimation.Result? Written,
        string Message);

    internal delegate NativeAnimation.Result Encoder(byte[] source, HkxAnimationData animation);
    internal delegate string Verifier(
        NativeAnimation.Result written,
        bool spline,
        HkxAnimationData animation,
        int editedTrack,
        int editedFrame,
        NativeAnimation.Timeline? timeline);

    internal static Result Commit(
        string path,
        HkxAnimationData animation,
        DocumentSourceStamp? openedStamp,
        int editedTrack = -1,
        int editedFrame = -1,
        NativeAnimation.Timeline? timeline = null,
        Func<Exception>? verificationFault = null,
        Action? beforeSourceRecheck = null,
        Encoder? encoder = null,
        Verifier? verifier = null)
    {
        ArgumentNullException.ThrowIfNull(animation);

        string[] kept = FileSafety.RecoverInterrupted(path);
        Result Refuse(string message) => Refused(message + FileSafety.KeptNote(kept));

        byte[] source;
        try
        {
            source = InputFilePolicy.ReadHkx(path);
        }
        catch (Exception error)
            when (error is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            return Refuse("Not saved: " + DocumentSourceStamp.ReadFailure(error));
        }

        if (openedStamp != null && !openedStamp.Matches(source, out string externalChange))
            return Refuse("Not saved: " + externalChange);

        var transactionStamp = DocumentSourceStamp.Capture(source);
        string? blocked = HkxTextEdit.WhyNotWritable(path);
        if (blocked != null)
            return Refuse("Cannot save: " + blocked);

        bool spline = animation.AnimationClass == NativeAnimation.SplineClass;
        NativeAnimation.Result written;
        try
        {
            written = encoder != null
                ? encoder(source, animation)
                : spline
                    ? NativeAnimation.Recompress(source, animation, null, true, timeline)
                    : NativeAnimation.Interleave(source, animation, timeline);
        }
        catch (Exception error)
        {
            return Refuse(
                "Not saved, and the original is untouched: " + error.Message);
        }

        string report;
        try
        {
            report = (verifier ?? Verify)(written, spline, animation, editedTrack, editedFrame, timeline);
            if (verificationFault != null) throw verificationFault();
        }
        catch (Exception error)
        {
            return Refuse(
                "The rebuilt animation failed verification, so nothing was written: " +
                error.Message);
        }

        beforeSourceRecheck?.Invoke();
        if (!transactionStamp.Matches(path, out externalChange))
            return Refuse("Not saved: " + externalChange);

        string? backup;
        try
        {
            backup = FileSafety.ReplaceChecked(path, written.Bytes, transactionStamp);
        }
        catch (Exception error)
        {
            return Refuse("Not saved: the file could not be written: " + error.Message);
        }

        string size = written.Grew >= 0
            ? $"{written.Grew} bytes larger"
            : $"{-written.Grew} bytes smaller";
        string original = backup == null
            ? ""
            : $" The original is kept as {Path.GetFileName(backup)}.";
        string message =
            $"Saved {written.Frames} frame(s) of {written.Tracks} track(s) " +
            (spline ? "spline compressed" : "uncompressed") +
            $", {size}.{original}{FileSafety.KeptNote(kept)}";

        if (!string.IsNullOrEmpty(report)) message += " " + report;

        return new Result(true, spline, written, message);
    }

    internal static string Verify(
        NativeAnimation.Result written,
        bool spline,
        HkxAnimationData animation,
        int editedTrack,
        int editedFrame,
        NativeAnimation.Timeline? timeline = null)
    {
        ArgumentNullException.ThrowIfNull(written);
        ArgumentNullException.ThrowIfNull(animation);

        var rebuilt = new HkxBinaryReader().ParseHkx(written.Bytes);
        if (rebuilt.HasUnsupportedAnimation)
            throw new InvalidDataException(
                $"the rebuilt file decodes as {rebuilt.AnimationClass}, which is not supported");

        var objects = new PackfileObjects(PackfileImage.Read(written.Bytes));
        var mismatched = HavokClassTypes.Shipped.SignatureProblems(objects.ClassNames());
        if (mismatched.Count > 0)
            throw new InvalidDataException(
                "rebuilt class signatures do not match: " + mismatched[0]);

        string expectedClass = spline
            ? NativeAnimation.SplineClass
            : NativeAnimation.InterleavedClass;
        if (rebuilt.AnimationClass != expectedClass)
            throw new InvalidDataException(
                $"rebuilt decodes as {rebuilt.AnimationClass}, expected {expectedClass}");
        string bound = NativeAnimation.SelectedAnimation(objects)?.ClassName ?? "no clip";
        if (bound != expectedClass)
            throw new InvalidDataException(
                $"rebuilt binds {bound}, expected {expectedClass}");
        if (rebuilt.NumTracks != written.Tracks)
            throw new InvalidDataException(
                $"rebuilt decodes to {rebuilt.NumTracks} track(s), expected {written.Tracks}");
        if (rebuilt.NumFrames != animation.NumFrames)
            throw new InvalidDataException(
                $"rebuilt decodes to {rebuilt.NumFrames} frame(s), expected {animation.NumFrames}");
        if (Math.Abs(rebuilt.Duration - animation.Duration) > 1e-3f)
            throw new InvalidDataException(
                $"rebuilt duration {rebuilt.Duration} differs from the edited {animation.Duration}");

        if (timeline?.Motion is { Any: true } intended)
        {
            var read = RootMotion.Read(written.Bytes);
            if (read.Samples.Count != intended.Samples.Count)
                throw new InvalidDataException(
                    $"the rebuilt root motion holds {read.Samples.Count} sample(s), and the edit left {intended.Samples.Count}");
            if (Math.Abs(read.Duration - intended.Duration) > 1e-3f)
                throw new InvalidDataException(
                    $"the rebuilt root motion lasts {read.Duration}s, and the edit left {intended.Duration}s");
            for (int i = 0; i < intended.Samples.Count; i++)
            {
                var wanted = intended.Samples[i];
                var actual = read.Samples[i];
                if ((actual.Position - wanted.Position).Length() > 1e-4f || MathF.Abs(actual.TurnRadians - wanted.TurnRadians) > 1e-4f)
                    throw new InvalidDataException($"the rebuilt root motion's sample {i} differs from the edit");
            }
        }

        if (rebuilt.Tracks.Count != animation.Tracks.Count)
            throw new InvalidDataException(
                $"rebuilt decodes to {rebuilt.Tracks.Count} track(s), expected {animation.Tracks.Count}");

        float clipLimit = spline ? 0.05f : 0.001f;
        float worstPosition = 0, worstRotation = 0, worstScale = 0;
        int atTrack = -1, atFrame = -1, frames = animation.NumFrames;

        for (int t = 0; t < animation.Tracks.Count; t++)
        {
            var asked = animation.Tracks[t];
            var got = rebuilt.Tracks[t];
            int landed = Math.Min(Math.Min(got.Translations.Count, got.Rotations.Count), got.Scales.Count);
            int held = Math.Min(Math.Min(asked.Translations.Count, asked.Rotations.Count), asked.Scales.Count);
            if (landed < frames || held < frames)
                throw new InvalidDataException(
                    $"track {t} holds {Math.Min(landed, held)} of the {frames} frame(s) the clip declares, " +
                    "so frames are missing");

            for (int f = 0; f < frames; f++)
            {
                if (!IsFinite(got.Translations[f]) || !IsFinite(got.Rotations[f]) || !IsFinite(got.Scales[f]) ||
                    !IsFinite(asked.Translations[f]) || !IsFinite(asked.Rotations[f]) || !IsFinite(asked.Scales[f]))
                    throw new InvalidDataException(
                        $"track {t} frame {f} holds a value that is not finite, so it cannot be checked");

                float position = (got.Translations[f] - asked.Translations[f]).Length();
                float turn = SplineQuat.AngleBetween(got.Rotations[f], asked.Rotations[f]);
                if (!float.IsFinite(turn))
                    throw new InvalidDataException(
                        $"track {t} frame {f} holds a rotation of zero length, which turns nothing, so its turn cannot be checked");
                float scale = (got.Scales[f] - asked.Scales[f]).Length();

                if (atTrack < 0 || position > worstPosition || turn > worstRotation || scale > worstScale)
                { atTrack = t; atFrame = f; }
                worstPosition = MathF.Max(worstPosition, position);
                worstRotation = MathF.Max(worstRotation, turn);
                worstScale = MathF.Max(worstScale, scale);
            }
        }

        if (worstPosition > clipLimit || worstRotation > clipLimit || worstScale > clipLimit)
            throw new InvalidDataException(
                $"the rebuilt clip does not hold the frames it was written from: track {atTrack} " +
                $"frame {atFrame} moved {worstPosition:F4} unit(s), turned {worstRotation:F5} radian(s) and " +
                $"scaled {worstScale:F4}, past the {clipLimit} a clip this kind can be re-encoded by");

        string report = "";

        if (timeline != null)
        {
            if (rebuilt.Annotations.Count != animation.Annotations.Count)
                throw new InvalidDataException(
                    $"the rebuilt clip holds {rebuilt.Annotations.Count} annotation(s), and the edit " +
                    $"left {animation.Annotations.Count}, so the file and the editor would disagree");

            float noteSlack = timeline.Slack * timeline.Scale;
            var wantedNotes = animation.Annotations.OrderBy(note => note.Time).ToList();
            var landedNotes = rebuilt.Annotations.OrderBy(note => note.Time).ToList();

            for (int n = 0; n < landedNotes.Count; n++)
            {
                if (landedNotes[n].Text != wantedNotes[n].Text)
                    throw new InvalidDataException(
                        $"the rebuilt clip's annotation {n} reads \"{landedNotes[n].Text}\" where the " +
                        $"edit left \"{wantedNotes[n].Text}\", so the two moved different notes");

                float moved = Math.Abs(landedNotes[n].Time - wantedNotes[n].Time);
                if (moved > noteSlack + 1e-4f)
                    throw new InvalidDataException(
                        $"the rebuilt clip's annotation {n} landed at {landedNotes[n].Time:F4}s where " +
                        $"the edit left it at {wantedNotes[n].Time:F4}s, {moved:F4}s out, past the " +
                        $"{noteSlack:F4}s a note in a clip this kind may move by");
            }

            if (atTrack >= 0)
                report = $"The worst rebuilt frame is {worstPosition:F4} unit(s), {worstRotation:F5} radian(s) " +
                         $"and {worstScale:F4} scale from the edit, on track {atTrack} frame {atFrame}.";
        }

        return report;
    }

    private static bool IsFinite(Vector3 v) =>
        float.IsFinite(v.X) && float.IsFinite(v.Y) && float.IsFinite(v.Z);

    private static bool IsFinite(Quaternion q) =>
        float.IsFinite(q.X) && float.IsFinite(q.Y) && float.IsFinite(q.Z) && float.IsFinite(q.W);

    private static Result Refused(string message) =>
        new(false, false, null, message);
}
