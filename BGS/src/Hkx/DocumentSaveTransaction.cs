using System;
using System.IO;

namespace OpenCommonwealth.Services.Hkx;

internal static class DocumentSaveTransaction
{
    internal enum Outcome
    {
        Refused,
        Unchanged,
        Committed,
    }

    internal sealed record Result(
        Outcome State,
        NativeSave.Plan? Plan,
        string Message,
        DocumentSourceStamp? Stamp = null)
    {
        internal bool Committed => State == Outcome.Committed;
        internal bool Unchanged => State == Outcome.Unchanged;
    }

    internal static Result Commit(
        string path,
        string savedXml,
        string currentXml,
        DocumentSourceStamp? openedStamp,
        Func<Exception>? verificationFault = null,
        Action? beforeSourceRecheck = null)
    {
        string[] kept = FileSafety.RecoverInterrupted(path);
        Result Refuse(string message) => Refused(message + FileSafety.KeptNote(kept));

        byte[] source;
        try
        {
            source = InputFilePolicy.ReadHkx(path);
        }
        catch (Exception error)
        {
            return Refuse("Not saved: " + DocumentSourceStamp.ReadFailure(error));
        }

        if (openedStamp != null && !openedStamp.Matches(source, out string externalChange))
            return Refuse("Not saved: " + externalChange);

        var transactionStamp = DocumentSourceStamp.Capture(source);

        NativeSave.Plan plan;
        try
        {
            plan = NativeSave.Compare(savedXml, currentXml);
        }
        catch (Exception error)
        {
            return Refuse(
                "Could not work out what changed, so nothing was written: " + error.Message);
        }

        if (!plan.Possible)
            return Refuse(plan.Refusal ?? "native save does not support this edit yet");
        if (plan.Empty)
            return new Result(Outcome.Unchanged, plan, "Nothing to save.");

        string? blocked = HkxTextEdit.WhyNotWritable(path);
        if (blocked != null)
            return Refuse("Cannot save: " + blocked);

        byte[] rebuilt;
        try
        {
            rebuilt = NativeSave.Apply(source, plan);
        }
        catch (Exception error)
        {
            return Refuse(
                "Not saved, and the original is untouched: " + error.Message);
        }

        try
        {
            SaveVerifier.Verify(source, rebuilt, plan);
            if (verificationFault != null) throw verificationFault();
        }
        catch (Exception error)
        {
            return Refuse(
                "The rebuilt file failed verification, so nothing was written: " + error.Message);
        }

        beforeSourceRecheck?.Invoke();
        if (!transactionStamp.Matches(path, out externalChange))
            return Refuse("Not saved: " + externalChange);

        string? backup;
        try
        {
            backup = FileSafety.ReplaceChecked(path, rebuilt, transactionStamp);
        }
        catch (Exception error)
        {
            return Refuse("Not saved: the file could not be written: " + error.Message);
        }

        return new Result(Outcome.Committed, plan, SuccessMessage(plan, backup, kept),
                          DocumentSourceStamp.Capture(rebuilt));
    }

    private static Result Refused(string message) =>
        new(Outcome.Refused, null, message);

    private static string SuccessMessage(NativeSave.Plan plan, string? backup, string[] kept)
    {
        string how = plan.Gone.Count > 0
            ? $"and took out {plan.Gone.Count} object{(plan.Gone.Count == 1 ? "" : "s")}, " +
              "so the file was laid out again and everything after them has moved. Object " +
              "numbers above the ones deleted have changed. "
            : plan.Grows
                ? "with anything that grew added on the end so nothing already in it moved. "
                : "leaving every other byte as it was. ";

        string original = backup == null
            ? ""
            : $"The original is kept as {Path.GetFileName(backup)}.";
        return $"Saved {plan.Changes.Count} " +
               $"change{(plan.Changes.Count == 1 ? "" : "s")} straight into the file, " + how +
               original + FileSafety.KeptNote(kept);
    }
}
