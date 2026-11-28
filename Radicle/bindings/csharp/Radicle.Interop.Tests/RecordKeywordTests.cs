using System;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class RecordKeywordTests
{
    private const uint Two = 0x910, None = 0x912, BadCount = 0x913, NullEntry = 0x914, Twice = 0x915, Quest = 0x917;
    private const uint KeywordA = 0x920, KeywordB = 0x921;

    private static FormKey Key(uint local) => new("Base.esm", local);

    private static Rec Npc(uint id, params (string, byte[])[] extra) =>
        new("NPC_", id, new[] { ("EDID", Z("Npc")) }.Concat(extra).ToArray());

    private static RadicleEnvironment Load(TempDirectory temp) =>
        RadicleEnvironment.Load(new[] { temp.Write("Base.esm", Build(0x1, Array.Empty<string>(),
            Top("NPC_",
                Npc(Two, ("KSIZ", U32(2)), ("KWDA", U32(KeywordB, KeywordA))),
                Npc(None),
                Npc(BadCount, ("KSIZ", U32(3)), ("KWDA", U32(KeywordA))),
                Npc(NullEntry, ("KSIZ", U32(2)), ("KWDA", U32(0, KeywordA))),
                Npc(Twice, ("KSIZ", U32(1)), ("KWDA", U32(KeywordA)), ("KWDA", U32(KeywordB)))),
            Top("QUST", new Rec("QUST", Quest, new[] { ("EDID", Z("Quest")), ("KSIZ", U32(1)), ("KWDA", U32(KeywordA)) })))) });

    private static Status StatusOf(Action action) => Assert.Throws<RadicleException>(action).Status;

    [Fact]
    public void KeywordsComeBackInStoredOrder()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        Assert.Equal(new[] { Key(KeywordB), Key(KeywordA) }, env.RecordKeywords(env.Winner(Key(Two))!.Value));
    }

    [Fact]
    public void RecordWithoutKwdaAnswersAnEmptyList()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        Assert.Empty(env.RecordKeywords(env.Winner(Key(None))!.Value));
    }

    [Fact]
    public void NullKeywordEntriesAreDropped()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        Assert.Equal(new[] { Key(KeywordA) }, env.RecordKeywords(env.Winner(Key(NullEntry))!.Value));
    }

    [Fact]
    public void MalformedKeywordsAreParseErrorsAndAQuestIsRefused()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        Assert.Equal(Status.ParseError, StatusOf(() => env.RecordKeywords(env.Winner(Key(BadCount))!.Value)));
        Assert.Equal(Status.ParseError, StatusOf(() => env.RecordKeywords(env.Winner(Key(Twice))!.Value)));
        Assert.Equal(Status.InvalidArgument, StatusOf(() => env.RecordKeywords(env.Winner(Key(Quest))!.Value)));
    }
}
