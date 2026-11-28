using System;
using System.IO;

namespace OpenCommonwealth.Services.Archive;

public sealed partial class GameData
{
    private static readonly EnumerationOptions LooseEnumeration = new() { RecurseSubdirectories = true, IgnoreInaccessible = true, AttributesToSkip = 0 };

    public byte[]? TryReadEntry(string archivePath, Ba2.Entry entry)
    {
        try { return Open(archivePath).Read(entry); }
        catch (Exception e) when (IsSkippableArchiveFailure(e)) { return null; }
    }
}
