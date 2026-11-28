using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record MessageRecord(string? Title, string? Body, IReadOnlyList<string> Buttons, bool IsMessageBox,
                                   bool DelayInitialDisplay, uint? DisplayTime, string? Swf, FormKey? OwnerQuest);

public sealed unsafe partial class RadicleEnvironment
{
    private static readonly Encoding Windows1252 = CodePagesEncodingProvider.Instance.GetEncoding(1252)!;

    public MessageRecord Message(Record record, StringTables? strings = null)
    {
        var info = Info(record);
        if (info.Signature != "MESG") throw new ArgumentException("The record is not a MESG.", nameof(record));
        if (strings is not null && strings.Owner != this)
            throw new ArgumentException("The strings do not belong to this environment.", nameof(strings));
        EnvPluginInfo pluginInfo;
        using (var use = Use()) Call.Check(RadicleNative.EnvGetPluginInfo(use.Pointer, (nuint)info.PluginIndex, &pluginInfo));
        var localized = pluginInfo.Localized != 0;
        var subs = ReadPayload(record).Subrecords;
        Subrecord? First(string signature) => subs.FirstOrDefault(s => s.Signature == signature);
        uint? U32(string signature) => First(signature) is { Bytes.Length: >= 4 } s ? BitConverter.ToUInt32(s.Bytes) : null;
        string? Text(Subrecord? sub)
        {
            if (sub is null) return null;
            if (!localized) return InlineMessageText(sub.Bytes);
            if (strings is null || sub.Bytes.Length != 4) return null;
            var text = strings.Lookup(info.PluginIndex, StringTables.SourceFor("MESG", sub.Signature), BitConverter.ToUInt32(sub.Bytes));
            return text is null ? null : Encoding.UTF8.GetString(text);
        }
        var flags = U32("DNAM") ?? 0;
        FormKey? ownerQuest = null;
        if (U32("QNAM") is { } raw)
        {
            try { ownerQuest = ResolveFormId(record, raw); }
            catch (RadicleException failure) when (failure.Status == Status.ParseError) { }
        }
        return new(Text(First("FULL")), Text(First("DESC")),
            subs.Where(s => s.Signature == "ITXT").Select(s => Text(s) ?? string.Empty).ToArray(),
            (flags & 1) != 0, (flags & 2) != 0, U32("TNAM"),
            First("SNAM") is { } swf ? InlineMessageText(swf.Bytes) : null,
            ownerQuest);
    }

    private static string InlineMessageText(byte[] bytes)
    {
        var end = Array.IndexOf(bytes, (byte)0);
        return Windows1252.GetString(bytes, 0, end < 0 ? bytes.Length : end);
    }
}
