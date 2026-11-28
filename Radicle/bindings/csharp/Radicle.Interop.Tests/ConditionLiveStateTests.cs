using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class ConditionLiveStateTests
{
    private const uint Plain = 0xA10, Constant = 0xA11, NoValue = 0xA12, Race = 0xA20, Quest = 0xA30, Keyword = 0xA31,
        Faction = 0xA32, Reference = 0xA40, Item = 0xA50;
    private const uint ConstantFlag = 0x40;
    private const byte Equal = 0x00, GreaterEqual = 0x60, Or = 0x01, Aliases = 0x02;

    private static FormKey Key(uint local) => new("Base.esm", local);

    private static RadicleEnvironment Load(TempDirectory temp) =>
        RadicleEnvironment.Load(new[] { temp.Write("Base.esm", Build(0x1, Array.Empty<string>(),
            Top("RACE", new Rec("RACE", Race, new[] { ("EDID", Z("Race")) })),
            Top("QUST", new Rec("QUST", Quest, new[] { ("EDID", Z("Quest")) })),
            Top("GLOB",
                new Rec("GLOB", Plain, new[] { ("EDID", Z("Plain")), ("FNAM", new byte[] { (byte)'f' }), ("FLTV", BitConverter.GetBytes(3.0f)) }),
                new Rec("GLOB", Constant, new[] { ("EDID", Z("Const")), ("FNAM", new byte[] { (byte)'f' }), ("FLTV", BitConverter.GetBytes(0.5f)) }, ConstantFlag),
                new Rec("GLOB", NoValue, new[] { ("EDID", Z("NoValue")), ("FNAM", new byte[] { (byte)'f' }) })))) });

    private static RadicleCondition Row(string function, byte flags, float comparison, int param1 = 0, int param2 = 0,
        uint runOn = 0, uint reference = 0, int param3 = 0) =>
        new(flags, BitConverter.SingleToUInt32Bits(comparison), ConditionFunctions.Id(function)!.Value, 0, param1, param2,
            runOn, reference, param3, null, null);

    private sealed class Fake : IConditionLiveState
    {
        public double? LevelValue = null;
        public LiveSubject? LastSubject;
        public bool? InFactionValue = null;
        public bool? HasKeywordValue = null;
        public bool? StageDoneValue = null;
        public Func<FormKey, double?>? GlobalOf;
        public int? SexValue = null;
        public int? ItemCountValue = null;
        public bool? AliasValue = null;
        public readonly List<string> Seen = new();

        public double? Level(LiveSubject subject)
        {
            LastSubject = subject;
            Seen.Add($"level:{subject.RunOn}:{subject.Reference}");
            return LevelValue;
        }

        public bool? InFaction(LiveSubject subject, FormKey faction)
        {
            Seen.Add($"faction:{faction}");
            return InFactionValue;
        }

        public bool? HasKeyword(LiveSubject subject, FormKey keyword)
        {
            Seen.Add($"keyword:{keyword}");
            return HasKeywordValue;
        }

        public bool? QuestStageDone(FormKey quest, int stage)
        {
            Seen.Add($"stagedone:{quest}:{stage}");
            return StageDoneValue;
        }

        public double? GlobalValue(FormKey global)
        {
            Seen.Add($"global:{global}");
            return GlobalOf?.Invoke(global);
        }

        public int? SexOf(LiveSubject subject)
        {
            LastSubject = subject;
            Seen.Add($"sex:{subject.RunOn}");
            return SexValue;
        }

        public int? ItemCount(LiveSubject subject, FormKey item)
        {
            Seen.Add($"count:{item}");
            return ItemCountValue;
        }

        public bool? IsAliasRef(LiveSubject subject, FormKey? quest, int alias)
        {
            Seen.Add($"alias:{quest}:{alias}");
            return AliasValue;
        }
    }

    [Fact]
    public void HostLevelDecidesATrueAFalseAndAnUnknownRow()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var record = env.Winner(Key(Race))!.Value;
        var rows = new[] { Row("GetLevel", GreaterEqual, 10f) };
        Assert.Equal(RuleVerdict.True, env.EvaluateConditions(record, rows, new Fake { LevelValue = 12 }).Verdict);
        Assert.Equal(RuleVerdict.False, env.EvaluateConditions(record, rows, new Fake { LevelValue = 5 }).Verdict);
        var unknown = env.EvaluateConditions(record, rows, new Fake());
        Assert.Equal(RuleVerdict.Unevaluable, unknown.Verdict);
        Assert.Equal(0UL, unknown.FirstUnevaluableRow);
    }

    [Fact]
    public void FormParametersReachTheHostAsKeysAndAnOrGroupIsTrueWhenOneMemberIsKnownTrue()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var record = env.Winner(Key(Race))!.Value;
        var rows = new[]
        {
            Row("HasKeyword", (byte)(Equal | Or), 1f, (int)Keyword),
            Row("GetInFaction", Equal, 1f, (int)Faction),
        };
        var fake = new Fake { InFactionValue = true };
        Assert.Equal(RuleVerdict.True, env.EvaluateConditions(record, rows, fake).Verdict);
        Assert.Equal(new[] { $"keyword:{Key(Keyword)}", $"faction:{Key(Faction)}" }, fake.Seen);
    }

    [Fact]
    public void SexIsComparedWithTheRowsSexParameterAndAnUnknownSexIsUnevaluable()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var record = env.Winner(Key(Race))!.Value;
        var female = new Fake { SexValue = 1 };
        Assert.Equal(RuleVerdict.True, env.EvaluateConditions(record, new[] { Row("GetIsSex", Equal, 1f, param1: 1) }, female).Verdict);
        Assert.Equal(RuleVerdict.False, env.EvaluateConditions(record, new[] { Row("GetIsSex", Equal, 1f, param1: 0) }, female).Verdict);
        Assert.Equal(RuleVerdict.True, env.EvaluateConditions(record, new[] { Row("GetIsSex", Equal, 0f, param1: 0) }, new Fake { SexValue = 1 }).Verdict);
        var unknown = new Fake();
        Assert.Equal(RuleVerdict.Unevaluable, env.EvaluateConditions(record, new[] { Row("GetIsSex", Equal, 1f, param1: 1) }, unknown).Verdict);
        Assert.Equal(new[] { "sex:0" }, unknown.Seen);
    }

    [Fact]
    public void SexCarriesTheRunOnToTheHost()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var fake = new Fake { SexValue = 0 };
        var outcome = env.EvaluateConditions(env.Winner(Key(Race))!.Value,
            new[] { Row("GetIsSex", Equal, 1f, param1: 0, runOn: 1) }, fake);
        Assert.Equal(RuleVerdict.True, outcome.Verdict);
        Assert.Equal(1u, fake.LastSubject!.Value.RunOn);
    }

    [Fact]
    public void ItemCountPassesTheItemKeyAndTheHostCountIsComparedAndNullIsUnevaluable()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var record = env.Winner(Key(Race))!.Value;
        var rows = new[] { Row("GetItemCount", GreaterEqual, 3f, (int)Item) };
        var many = new Fake { ItemCountValue = 5 };
        Assert.Equal(RuleVerdict.True, env.EvaluateConditions(record, rows, many).Verdict);
        Assert.Equal(new[] { $"count:{Key(Item)}" }, many.Seen);
        Assert.Equal(RuleVerdict.False, env.EvaluateConditions(record, rows, new Fake { ItemCountValue = 2 }).Verdict);
        Assert.Equal(RuleVerdict.Unevaluable, env.EvaluateConditions(record, rows, new Fake()).Verdict);
    }

    [Fact]
    public void ItemCountWithANullItemNeverReachesTheHost()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var fake = new Fake { ItemCountValue = 5 };
        var outcome = env.EvaluateConditions(env.Winner(Key(Race))!.Value,
            new[] { Row("GetItemCount", GreaterEqual, 1f, 0) }, fake);
        Assert.Equal(RuleVerdict.Unevaluable, outcome.Verdict);
        Assert.Empty(fake.Seen);
    }

    [Fact]
    public void AliasRefPassesTheRawAliasAndTheQuestOfAQuestRowAndNullForOtherRecords()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var rows = new[] { Row("GetIsAliasRef", Equal, 1f, 7) };
        var onQuest = new Fake { AliasValue = true };
        Assert.Equal(RuleVerdict.True, env.EvaluateConditions(env.Winner(Key(Quest))!.Value, rows, onQuest).Verdict);
        Assert.Equal(new[] { $"alias:{Key(Quest)}:7" }, onQuest.Seen);
        var onRace = new Fake { AliasValue = false };
        Assert.Equal(RuleVerdict.False, env.EvaluateConditions(env.Winner(Key(Race))!.Value, rows, onRace).Verdict);
        Assert.Equal(new[] { "alias::7" }, onRace.Seen);
        Assert.Equal(RuleVerdict.Unevaluable, env.EvaluateConditions(env.Winner(Key(Quest))!.Value, rows, new Fake()).Verdict);
    }

    [Fact]
    public void ARowThatSwitchesItsParametersToAnAliasIsStillRefusedForTheNewFacts()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var fake = new Fake { SexValue = 1, ItemCountValue = 1, AliasValue = true };
        var outcome = env.EvaluateConditions(env.Winner(Key(Quest))!.Value, new[]
        {
            Row("GetIsSex", (byte)(Equal | Aliases), 1f, 1),
            Row("GetItemCount", (byte)(Equal | Aliases), 1f, (int)Item),
            Row("GetIsAliasRef", (byte)(Equal | Aliases), 1f, 7),
        }, fake);
        Assert.Equal(RuleVerdict.Unevaluable, outcome.Verdict);
        Assert.Empty(fake.Seen);
    }

    [Fact]
    public void StageDonePassesTheQuestKeyAndTheStageNumber()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var fake = new Fake { StageDoneValue = true };
        var outcome = env.EvaluateConditions(env.Winner(Key(Race))!.Value,
            new[] { Row("GetStageDone", Equal, 1f, (int)Quest, 10) }, fake);
        Assert.Equal(RuleVerdict.True, outcome.Verdict);
        Assert.Equal(new[] { $"stagedone:{Key(Quest)}:10" }, fake.Seen);
    }

    [Fact]
    public void UseGlobalComparisonIsReadFromTheHostAndAnUnknownGlobalIsUnevaluable()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var record = env.Winner(Key(Race))!.Value;
        var rows = new[] { new RadicleCondition(0x04 | 0x40, Plain, ConditionFunctions.Id("GetLevel")!.Value, 0, 0, 0, 0, 0, 0, null, null) };
        var known = new Fake { LevelValue = 7, GlobalOf = _ => 5 };
        Assert.Equal(RuleVerdict.True, env.EvaluateConditions(record, rows, known).Verdict);
        Assert.Contains($"global:{Key(Plain)}", known.Seen);
        Assert.Equal(RuleVerdict.Unevaluable, env.EvaluateConditions(record, rows, new Fake { LevelValue = 7 }).Verdict);
    }

    [Fact]
    public void RunOnAndTheResolvedReferenceReachTheHostAndFeedTheKernelUnchanged()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var record = env.Winner(Key(Race))!.Value;
        var rows = new[] { Row("GetLevel", GreaterEqual, 1f, runOn: 2, reference: Reference) };
        var fake = new Fake { LevelValue = 3 };
        var inputs = env.ConditionInputs(record, rows, fake);
        Assert.Equal(new[] { $"level:2:{Key(Reference)}" }, fake.Seen);
        var input = Assert.Single(inputs);
        Assert.True(input.Known);
        Assert.Equal(3.0, input.Value);
        Assert.Equal((2u, Reference), (input.ResolvedRunOn, input.ResolvedReference));
        var rules = rows.Select(r => new ConditionRule(r.FlagsByte, r.ComparisonRaw, r.Function, r.RunOn, r.Reference)).ToArray();
        Assert.Equal(RuleVerdict.True, PackageRules.EvaluateConditions(rules, inputs).Verdict);
    }

    [Fact]
    public void SelectorRunOnsCarryTheRawReferenceAndTheThirdParameterToTheHost()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var fake = new Fake { LevelValue = 3 };
        var outcome = env.EvaluateConditions(env.Winner(Key(Race))!.Value,
            new[] { Row("GetLevel", GreaterEqual, 1f, runOn: 5, reference: 0x77, param3: 1234) }, fake);
        Assert.Equal(RuleVerdict.True, outcome.Verdict);
        var subject = fake.LastSubject!.Value;
        Assert.Equal((5u, 0x77u, 1234), (subject.RunOn, subject.ReferenceRaw, subject.Param3));
    }

    [Fact]
    public void UseGlobalComparisonFallsBackToAConstantGlobalsRecordValue()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var rows = new[] { new RadicleCondition(0x04 | 0x40, Constant, ConditionFunctions.Id("GetLevel")!.Value, 0, 0, 0, 0, 0, 0, null, null) };
        Assert.Equal(RuleVerdict.True,
            env.EvaluateConditions(env.Winner(Key(Race))!.Value, rows, new Fake { LevelValue = 1 }).Verdict);
    }

    [Fact]
    public void RowsThatNeedMoreContextAreUnknownAndNeverReachTheHost()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var fake = new Fake { LevelValue = 99 };
        var outcome = env.EvaluateConditions(env.Winner(Key(Race))!.Value,
            new[] { Row("GetLevel", (byte)(Equal | Aliases), 1f) }, fake);
        Assert.Equal(RuleVerdict.Unevaluable, outcome.Verdict);
        Assert.Empty(fake.Seen);
    }

    [Fact]
    public void ANullProviderIsRefusedBeforeTheNativeCall()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        Assert.Throws<ArgumentNullException>(() => env.EvaluateConditions(env.Winner(Key(Race))!.Value,
            Array.Empty<RadicleCondition>(), null!));
    }

    [Fact]
    public void AnEmptyListIsTrueWithoutAskingTheHost()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var fake = new Fake();
        Assert.Equal(RuleVerdict.True,
            env.EvaluateConditions(env.Winner(Key(Race))!.Value, Array.Empty<RadicleCondition>(), fake).Verdict);
        Assert.Empty(fake.Seen);
    }

    [Fact]
    public void AProviderExceptionIsRethrownToTheCallerAfterTheNativeCallReturns()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var record = env.Winner(Key(Race))!.Value;
        var host = new Throwing();
        var error = Assert.Throws<InvalidOperationException>(() => env.EvaluateConditions(record,
            new[] { Row("GetLevel", GreaterEqual, 1f), Row("GetLevel", GreaterEqual, 2f) }, host));
        Assert.Equal("host failed", error.Message);
        Assert.Equal(1, host.Calls);
    }

    [Fact]
    public void LiveGlobalAsksTheHostFirstAndOnlyAConstantFallsBackToTheRecord()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var plain = env.Winner(Key(Plain))!.Value;
        var constant = env.Winner(Key(Constant))!.Value;
        var empty = env.Winner(Key(NoValue))!.Value;

        var hosted = new Fake { GlobalOf = _ => 9.5 };
        Assert.Equal(new LiveGlobalValueResult(9.5, LiveGlobalSource.Host), env.LiveGlobalValue(plain, hosted));
        Assert.Equal(new LiveGlobalValueResult(9.5, LiveGlobalSource.Host), env.LiveGlobalValue(constant, hosted));

        var silent = new Fake();
        Assert.Equal(new LiveGlobalValueResult(null, LiveGlobalSource.NoValue), env.LiveGlobalValue(plain, silent));
        Assert.Equal(new LiveGlobalValueResult(0.5, LiveGlobalSource.RecordConstant), env.LiveGlobalValue(constant, silent));
        Assert.Equal(new LiveGlobalValueResult(null, LiveGlobalSource.NoValue), env.LiveGlobalValue(empty, silent));
        Assert.Equal(new LiveGlobalValueResult(null, LiveGlobalSource.NotGlob),
            env.LiveGlobalValue(env.Winner(Key(Race))!.Value, silent));
    }

    [Fact]
    public void LiveGlobalNarrowsTheHostValueToFloat()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var result = env.LiveGlobalValue(env.Winner(Key(Plain))!.Value, new Fake { GlobalOf = _ => 0.1 });
        Assert.Equal((double)0.1f, result.Value);
    }

    [Fact]
    public void VanillaPlacedDoorsExposeNamedLockLevelsAndTheLeveledFlag()
    {
        var dataDir = Environment.GetEnvironmentVariable("FO4_DATA_DIR");
        if (string.IsNullOrEmpty(dataDir)) return;
        var master = Path.Combine(dataDir, "Fallout4.esm");
        if (!File.Exists(master)) return;
        using var env = RadicleEnvironment.Load(new[] { master });
        var locked = 0;
        var requiresKey = 0;
        var undecodable = 0;
        foreach (var reference in env.WinnersOf("REFR"))
        {
            LockData lockData;
            try { lockData = env.Lock(reference); }
            catch (RadicleException)
            {
                ++undecodable;
                continue;
            }
            if (!lockData.HasLockData) continue;
            ++locked;
            Assert.Equal(NamedBand(lockData.Level), lockData.Kind);
            Assert.Equal((lockData.Flags & 0x04) != 0, lockData.LeveledLock);
            if (lockData.Level != 255) continue;
            ++requiresKey;
            Assert.Equal(LockKind.RequiresKey, lockData.Kind);
        }
        Assert.Equal(0, undecodable);
        Assert.True(locked > 0);
        Assert.True(requiresKey > 0);
    }

    private static LockKind NamedBand(byte level) =>
        level switch
        {
            0 => LockKind.None,
            < 50 => LockKind.Novice,
            < 75 => LockKind.Advanced,
            < 100 => LockKind.Expert,
            < 251 => LockKind.Master,
            251 => LockKind.Barred,
            252 => LockKind.Chained,
            253 => LockKind.RequiresTerminal,
            254 => LockKind.Inaccessible,
            _ => LockKind.RequiresKey,
        };

    private sealed class Throwing : IConditionLiveState
    {
        public int Calls;

        public double? Level(LiveSubject subject)
        {
            ++Calls;
            throw new InvalidOperationException("host failed");
        }
    }
}
