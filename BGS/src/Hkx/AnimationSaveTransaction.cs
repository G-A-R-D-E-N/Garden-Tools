using System;
using System.IO;
using System.Linq;

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

        FileSafety.RecoverInterrupted(path);

        byte[] source;
        try
        {
            source = InputFilePolicy.ReadHkx(path);
        }
        catch (Exception error)
            when (error is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            return Refused("Not saved: " + DocumentSourceStamp.ReadFailure(error));
        }

        if (openedStamp != null && !openedStamp.Matches(source, out string externalChange))
            return Refused("Not saved: " + externalChange);

        var transactionStamp = DocumentSourceStamp.Capture(source);
        string? blocked = HkxTextEdit.WhyNotWritable(path);
        if (blocked != null)
            return Refused("Cannot save: " + blocked);

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
            return Refused(
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
            return Refused(
                "The rebuilt animation failed verification, so nothing was written: " +
                error.Message);
        }

        beforeSourceRecheck?.Invoke();
        if (!transactionStamp.Matches(path, out externalChange))
            return Refused("Not saved: " + externalChange);

        try
        {
            FileSafety.ReplaceChecked(path, written.Bytes, transactionStamp);
        }
        catch (Exception error)
        {
            return Refused("Not saved: the file could not be written: " + error.Message);
        }

        string size = written.Grew >= 0
            ? $"{written.Grew} bytes larger"
            : $"{-written.Grew} bytes smaller";
        string message =
            $"Saved {written.Frames} frame(s) of {written.Tracks} track(s) " +
            (spline ? "spline compressed" : "uncompressed") +
            $", {size}. The original is kept as {Path.GetFileName(path + ".bak")}.";

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
        if (rebuilt.NumTracks != written.Tracks)
            throw new InvalidDataException(
                $"rebuilt decodes to {rebuilt.NumTracks} track(s), expected {written.Tracks}");
        if (rebuilt.NumFrames != written.Frames)
            throw new InvalidDataException(
                $"rebuilt decodes to {rebuilt.NumFrames} frame(s), expected {written.Frames}");
        if (Math.Abs(rebuilt.Duration - animation.Duration) > 1e-3f)
            throw new InvalidDataException(
                $"rebuilt duration {rebuilt.Duration} differs from the edited {animation.Duration}");

        string report = "";

        if (timeline != null)
        {
            float clipLimit = spline ? 0.05f : 0.001f;
            float worstPosition = 0, worstRotation = 0;
            int atTrack = -1, atFrame = -1, compared = 0;

            for (int t = 0; t < rebuilt.Tracks.Count && t < animation.Tracks.Count; t++)
            {
                var asked = animation.Tracks[t];
                var got = rebuilt.Tracks[t];
                int frames = Math.Min(Math.Min(got.Translations.Count, asked.Translations.Count),
                                      Math.Min(got.Rotations.Count, asked.Rotations.Count));

                for (int f = 0; f < frames; f++)
                {
                    if (compared == 0) { atTrack = t; atFrame = f; }
                    compared++;

                    float distance = (got.Translations[f] - asked.Translations[f]).Length();
                    if (distance > worstPosition) { worstPosition = distance; atTrack = t; atFrame = f; }

                    float angle = SplineQuat.AngleBetween(got.Rotations[f], asked.Rotations[f]);
                    if (angle > worstRotation) { worstRotation = angle; atTrack = t; atFrame = f; }
                }
            }

            if (worstPosition > clipLimit || worstRotation > clipLimit)
                throw new InvalidDataException(
                    $"the rebuilt clip does not hold the frames it was written from: track {atTrack} " +
                    $"frame {atFrame} moved {worstPosition:F4} unit(s) and {worstRotation:F5} radian(s), " +
                    $"past the {clipLimit} a clip this kind can be re-encoded by");

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
                report = $"The worst rebuilt frame is {worstPosition:F4} unit(s) and " +
                         $"{worstRotation:F5} radian(s) from the edit, on track {atTrack} frame " +
                         $"{atFrame}.";
        }

        if (editedTrack < 0 || editedFrame < 0 ||
            editedTrack >= rebuilt.Tracks.Count ||
            editedTrack >= animation.Tracks.Count ||
            editedFrame >= rebuilt.Tracks[editedTrack].Translations.Count ||
            editedFrame >= animation.Tracks[editedTrack].Translations.Count)
            return report;

        var wanted = animation.Tracks[editedTrack].Translations[editedFrame];
        var landed = rebuilt.Tracks[editedTrack].Translations[editedFrame];
        float drift = (landed - wanted).Length();
        float limit = spline ? 0.05f : 0.001f;
        if (drift > limit)
            throw new InvalidDataException(
                $"the edited frame did not survive re-encoding (drift {drift})");

        return report;
    }

    private static Result Refused(string message) =>
        new(false, false, null, message);
}
