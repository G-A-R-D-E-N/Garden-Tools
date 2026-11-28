using System;
using System.IO;
using System.Linq;
using System.Text;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class DialogueTests
{
    private static byte[] U16(ushort value) => BitConverter.GetBytes(value);
    private static byte[] Response(byte number) => U32(0x810).Concat(new[] { number }).Concat(U32(0x811))
        .Concat(new byte[] { 0xAB, 90, 0 }).Concat(U32(uint.MaxValue, 42)).ToArray();
    private static byte[] Condition(ushort function) => new byte[] { 0xE1, 0, 0, 0 }.Concat(U32(0x80000000))
        .Concat(U16(function)).Concat(U16(0)).Concat(U32(unchecked((uint)-7), 99, 0, 0, uint.MaxValue)).ToArray();

    [Fact]
    public void TopicGetterKeepsRawSubtypeBytesAndAbsentValues()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Dialogue.esm", Build(1, Array.Empty<string>(), Top("DIAL", new Rec("DIAL", 0x800,
            new[] { ("DATA", new byte[] { 0xA5, 0x80, 0xFF, 0xFE }), ("PNAM", F32(7.5f)), ("TIFC", U32(3)),
                ("SNAM", new byte[] { 67, 66, 65, 0 }), ("QNAM", U32(0x810)) }), new Rec("DIAL", 0x801, Array.Empty<(string, byte[])>()))));
        Radicle.Interop.DialogueTopic topic;
        using (var env = RadicleEnvironment.Load(new[] { path }))
        {
            topic = env.DialogueTopic(env.Winner(new FormKey("Dialogue.esm", 0x800))!.Value);
            Assert.Null(env.DialogueTopic(env.Winner(new FormKey("Dialogue.esm", 0x801))!.Value).Subtype);
        }
        Assert.Equal((ushort)0xFEFF, topic.Subtype); Assert.Equal((byte)0xA5, topic.Flags); Assert.Equal((byte)0x80, topic.Category);
        Assert.Equal(7.5f, topic.Priority); Assert.Equal(3u, topic.InfoCount); Assert.Equal(new byte[] { 67, 66, 65, 0 }, topic.SubtypeName);
        Assert.Equal(new FormKey("Dialogue.esm", 0x810), topic.Quest); Assert.Null(topic.Branch);
    }

    [Fact]
    public void InfoGetterCopiesScopedResponsesRawConditionsAndInlineText()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Dialogue.esm", Build(1, Array.Empty<string>(), Top("INFO", new Rec("INFO", 0x800, new[] {
            ("ENAM", U32(0xFFFF8001)), ("TRDA", Response(1)), ("NAM1", Z("first")), ("SNAM", U32(0x813)),
            ("TRDA", Response(2)), ("NAM1", Z("second")), ("LNAM", U32(0x814)), ("CTDA", Condition(14)),
            ("CIS1", Z("operand")), ("RNAM", Z("prompt")) }))));
        Radicle.Interop.DialogueInfo info;
        using (var env = RadicleEnvironment.Load(new[] { path }))
        {
            var record = env.Winner(new FormKey("Dialogue.esm", 0x800))!.Value;
            info = env.DialogueInfo(record);
            using var tables = StringTables.Load(env, temp.Path);
            Assert.Equal("first", Encoding.UTF8.GetString(tables.ResolveDialogueText(record, "NAM1", info.Responses[0].Text)!));
        }
        Assert.Equal(2, info.Responses.Count); Assert.Equal("first", info.Responses[0].Text.Text); Assert.Equal("second", info.Responses[1].Text.Text);
        Assert.Equal(new FormKey("Dialogue.esm", 0x813), info.Responses[0].SpeakerIdle); Assert.Null(info.Responses[1].SpeakerIdle);
        Assert.Equal(new FormKey("Dialogue.esm", 0x814), info.Responses[1].ListenerIdle); Assert.Equal(-1, info.Responses[0].CameraTargetAlias);
        Assert.Equal((ushort)0xFFFF, info.ResetTimeEncoded); Assert.Equal(24.0, info.ResetHours); Assert.Equal("prompt", info.Prompt.Text);
        var c = Assert.Single(info.Conditions); Assert.Equal((byte)0xE1, c.FlagsByte); Assert.Equal(-7, c.Param1); Assert.Equal("operand", c.Parameter1String);
    }

    [Fact]
    public void InfoTextIdsUseTheExistingIlAndNormalStringTables()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Dialogue.esm", Build(0x81, Array.Empty<string>(), Top("INFO", new Rec("INFO", 0x800,
            new[] { ("TRDA", Response(1)), ("NAM1", U32(123)), ("RNAM", U32(456)) }))));
        temp.Write("Dialogue_en.STRINGS", StringsWriter.Table((456, "prompt")));
        var il = U32(1, 13, 123, 0, 9).Concat(Z("response")).ToArray();
        temp.Write("Dialogue_en.ILSTRINGS", il);
        using var env = RadicleEnvironment.Load(new[] { path }); using var tables = StringTables.Load(env, temp.Path);
        var record = env.Winner(new FormKey("Dialogue.esm", 0x800))!.Value; var info = env.DialogueInfo(record);
        Assert.Equal(NameKind.StringId, info.Responses[0].Text.Kind); Assert.Equal(123u, info.Responses[0].Text.StringId);
        Assert.Equal("response", Encoding.ASCII.GetString(tables.ResolveDialogueText(record, "NAM1", info.Responses[0].Text)!));
        Assert.Null(info.ResetHours);
        Assert.Equal("prompt", Encoding.ASCII.GetString(tables.ResolveDialogueText(record, "RNAM", info.Prompt)!));
        Assert.Null(tables.ResolveDialogueText(record, "NAM1", new(NameKind.StringId, null, 999)));
    }

    [Fact]
    public void ResolveDialogueTextReturnsNullForAbsentAndMalformedAndUtf8ForInline()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Dialogue.esm", Build(0x81, Array.Empty<string>(), Top("INFO", new Rec("INFO", 0x800,
            new[] { ("TRDA", Response(1)), ("NAM1", U32(123)), ("RNAM", U32(456)) }))));
        temp.Write("Dialogue_en.STRINGS", StringsWriter.Table((456, "prompt")));
        temp.Write("Dialogue_en.ILSTRINGS", U32(1, 13, 123, 0, 9).Concat(Z("response")).ToArray());
        using var env = RadicleEnvironment.Load(new[] { path }); using var tables = StringTables.Load(env, temp.Path);
        var record = env.Winner(new FormKey("Dialogue.esm", 0x800))!.Value;

        Assert.Null(tables.ResolveDialogueText(record, "NAM1", new(NameKind.Absent, null, 123)));
        Assert.Null(tables.ResolveDialogueText(record, "NAM1", new(NameKind.Malformed, null, 123)));
        Assert.Equal(new byte[] { 0x63, 0x61, 0x66, 0xC3, 0xA9 }, tables.ResolveDialogueText(record, "NAM1", new(NameKind.Inline, "café", 0)));
    }

    [Fact]
    public void SceneGetterCopiesScopedGroupsAndOpaqueActionPayloads()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Dialogue.esm", Build(1, Array.Empty<string>(), Top("SCEN", new Rec("SCEN", 0x800, new[] {
            ("FNAM", U32(0x80000001)), ("HNAM", Array.Empty<byte>()), ("NAM0", Z("phase")), ("CTDA", Condition(14)),
            ("NEXT", Array.Empty<byte>()), ("CTDA", Condition(15)), ("NEXT", Array.Empty<byte>()), ("FNAM", U16(0x8001)),
            ("SCQS", U32(0x000AFFFF)), ("HNAM", Array.Empty<byte>()), ("ALID", U32(uint.MaxValue)), ("LNAM", U32(2)), ("DNAM", U32(4)),
            ("ANAM", U16(2)), ("NAM0", Z("action")), ("ALID", U32(8)), ("INAM", U32(9)), ("FNAM", U32(16)),
            ("SNAM", U32(65537)), ("ENAM", U32(uint.MaxValue)), ("SNAM", F32(10)), ("PNAM", U32(0x813)), ("ANAM", Array.Empty<byte>()),
            ("PNAM", U32(0x812)), ("CTDA", Condition(16)) }))));
        Radicle.Interop.Scene scene;
        using (var env = RadicleEnvironment.Load(new[] { path })) scene = env.Scene(env.Winner(new FormKey("Dialogue.esm", 0x800))!.Value);
        Assert.Equal(0x80000001u, scene.Flags); Assert.Equal(new FormKey("Dialogue.esm", 0x812), scene.ParentQuest);
        var phase = Assert.Single(scene.Phases); Assert.Equal((ushort)0x8001, phase.Flags); Assert.Equal((short)-1, phase.QuestStages!.Value.Start);
        Assert.Equal((ushort)14, Assert.Single(phase.StartConditions).Function); Assert.Equal((ushort)15, Assert.Single(phase.CompletionConditions).Function);
        Assert.Equal(-1, Assert.Single(scene.Actors).Alias); var action = Assert.Single(scene.Actions); Assert.Equal(8, action.ActorAlias);
        Assert.Equal(65537u, action.StartPhase); Assert.Equal(uint.MaxValue, action.EndPhase);
        Assert.Equal("SNAM", action.Payload[0].Signature); Assert.Equal(F32(10), action.Payload[0].Bytes);
        Assert.Equal("PNAM", action.Payload[1].Signature); Assert.Equal((ushort)16, Assert.Single(scene.Conditions).Function);
    }

    [Fact]
    public void SceneActionTypeNamesCoverTheNamedIdsAndLeaveUnknownIdsEmpty()
    {
        Assert.Equal("dialogue", RadicleEnvironment.SceneActionTypeName(0));
        Assert.Equal("player_dialogue", RadicleEnvironment.SceneActionTypeName(3));
        Assert.Equal("radio", RadicleEnvironment.SceneActionTypeName(6));
        Assert.Equal(string.Empty, RadicleEnvironment.SceneActionTypeName(7));
        Assert.Equal(string.Empty, RadicleEnvironment.SceneActionTypeName(0xFFFF));
    }

    private static (string, byte[])[] ActionGroup(ushort type, params (string, byte[])[] payload) =>
        new[] { ("ANAM", U16(type)), ("ALID", U32(1)), ("INAM", U32(0)), ("FNAM", U32(0x10000)), ("SNAM", U32(0)), ("ENAM", U32(1)) }
            .Concat(payload).Append(("ANAM", Array.Empty<byte>())).ToArray();
    private static (string, byte[])[] Cat(params (string, byte[])[][] parts) => parts.SelectMany(part => part).ToArray();
    private static byte[] Wide(string text) => U16((ushort)text.Length).Concat(Encoding.ASCII.GetBytes(text)).ToArray();
    private static FormKey Key(uint id) => new("Dialogue.esm", id);

    private static byte[] InfoVmad(byte flags, params (byte Unknown, string Function)[] rows)
    {
        var bytes = U16(5).Concat(U16(2)).Concat(U16(0)).Concat(new byte[] { 3, flags }).Concat(Wide("PF_Info"))
            .Concat(new byte[] { 0 }).Concat(U16(1)).Concat(Wide("Caps")).Concat(new byte[] { 1, 1 })
            .Concat(U16(0)).Concat(U16(0xFFFF)).Concat(U32(0xF));
        foreach (var (unknown, function) in rows) bytes = bytes.Concat(new[] { unknown }).Concat(Wide("PF_Info")).Concat(Wide(function));
        return bytes.ToArray();
    }

    [Fact]
    public void SceneExposesTheTailQuestStagesAndEveryActionTargetSubtype()
    {
        using var temp = new TempDirectory();
        var subs = Cat(
            ActionGroup(0, ("DATA", U32(0x810)), ("DMAX", F32(5f)), ("DMIN", F32(1f)), ("CRIS", F32(60f, 2f)), ("DEMO", U32(5)),
                ("DEVA", U32(40)), ("HTID", U32(2, uint.MaxValue)), ("VENC", U32(0x811)), ("ONAM", U32(0x812)), ("PNAM", U32(0x813))),
            ActionGroup(1, ("PNAM", U32(0x820)), ("PNAM", U32(0x821))),
            ActionGroup(2, ("SNAM", F32(9f)), ("SCQS", U16(20)), ("TNAM", F32(3f))),
            ActionGroup(3, ("PTOP", U32(0x830)), ("NTOP", U32(0x831)), ("VENC", U32(0x832)), ("DTID", U32(7)), ("ONAM", U32(0x833)),
                ("NPOT", U32(0x834)), ("NPOS", U32(0x835)), ("DTGT", U32(4))),
            ActionGroup(4, ("STSC", U32(0x850)), ("LCEP", U32(0x851)), ("INTT", U16(3)), ("SSPN", Z("Phase2")), ("CITC", U32(1)),
                ("CTDA", Condition(14)), ("LCEP", U32(0x852)), ("HTID", Array.Empty<byte>())),
            ActionGroup(5, ("PTOP", U32(0x840)), ("QTOP", U32(0x841)), ("DALC", U32(0x842)), ("DTID", U32(8, 9))),
            ActionGroup(6, ("DATA", U32(0x860)), ("HTID", U32(0x861)), ("DMAX", F32(7f)), ("VENC", U32(0x862))),
            new[] { ("PNAM", U32(0x812)), ("SCQS", U16(0xFFFF).Concat(U16(5)).ToArray()) });
        var path = temp.Write("Dialogue.esm", Build(1, Array.Empty<string>(), Top("SCEN", new Rec("SCEN", 0x800, subs))));
        Radicle.Interop.Scene scene;
        using (var env = RadicleEnvironment.Load(new[] { path })) scene = env.Scene(env.Winner(Key(0x800))!.Value);
        Assert.Equal(((short)-1, (short)5), scene.QuestStages!.Value);
        Assert.Equal(7, scene.Actions.Count);
        var dialogue = Assert.IsType<DialogueActionTarget>(scene.Actions[0].Target);
        Assert.False(dialogue.Malformed); Assert.Equal(Key(0x810), dialogue.Topic); Assert.Equal(Key(0x811), dialogue.DialogueSubtype);
        Assert.Equal(Key(0x812), dialogue.AudioOutputOverride); Assert.Equal(Key(0x813), dialogue.AnimArchType);
        Assert.Equal(5f, dialogue.LoopingMaxSeconds); Assert.Equal(1f, dialogue.LoopingMinSeconds);
        Assert.Equal(60f, dialogue.Camera!.Value.Fov); Assert.Equal(2f, dialogue.Camera!.Value.Rate);
        Assert.Equal(SceneEmotionType.Happy, dialogue.EmotionType); Assert.Equal(40u, dialogue.EmotionValue);
        Assert.Equal(new[] { 2, -1 }, dialogue.HeadtrackingAliases);
        var package = Assert.IsType<PackageActionTarget>(scene.Actions[1].Target);
        Assert.Equal(new[] { Key(0x820), Key(0x821) }, package.Packages);
        var timer = Assert.IsType<TimerActionTarget>(scene.Actions[2].Target);
        Assert.Equal(9f, timer.MaxSeconds); Assert.Equal(3f, timer.MinSeconds); Assert.Equal((short)20, timer.QuestStage);
        var player = Assert.IsType<PlayerDialogueActionTarget>(scene.Actions[3].Target);
        Assert.Equal(Key(0x830), player.PlayerResponses.Positive); Assert.Equal(Key(0x831), player.PlayerResponses.Negative);
        Assert.Equal(Key(0x832), player.PlayerSubtypes.Positive); Assert.Equal(Key(0x833), player.AudioOutputOverride);
        Assert.Equal(Key(0x834), player.NpcResponses.Positive); Assert.Equal(Key(0x835), player.NpcSubtypes.Positive);
        Assert.Null(player.PlayerResponses.Neutral); Assert.Equal(new[] { 7 }, player.NpcHeadtrackingAliases);
        Assert.Equal(4, player.DialogueTargetActor);
        var start = Assert.IsType<StartSceneActionTarget>(scene.Actions[4].Target);
        Assert.Equal(Key(0x850), start.Topic); Assert.True(start.EndSceneSayGreeting); Assert.Equal(2, start.Entries.Count);
        Assert.Equal(Key(0x851), start.Entries[0].Scene); Assert.Equal((ushort)3, start.Entries[0].PhaseIndex);
        Assert.Equal("Phase2", start.Entries[0].PhaseName); Assert.Equal((ushort)14, Assert.Single(start.Entries[0].Conditions).Function);
        Assert.Equal(Key(0x852), start.Entries[1].Scene); Assert.Null(start.Entries[1].PhaseName); Assert.Empty(start.Entries[1].Conditions);
        var reply = Assert.IsType<NpcResponseActionTarget>(scene.Actions[5].Target);
        Assert.Equal(Key(0x840), reply.PlayerResponses.Positive); Assert.Equal(Key(0x841), reply.PlayerResponses.Question);
        Assert.Equal(Key(0x842), reply.PlayerSubtypes.Question); Assert.Equal(new[] { 8, 9 }, reply.NpcHeadtrackingAliases);
        var radio = Assert.IsType<RadioActionTarget>(scene.Actions[6].Target);
        Assert.Equal(Key(0x860), radio.Topic); Assert.Equal(Key(0x861), radio.Sound); Assert.Equal(7f, radio.LoopingMaxSeconds);
        Assert.Equal(Key(0x862), radio.DialogueSubtype);
        Assert.All(scene.Actions, action => Assert.False(action.Target!.Malformed));
    }

    [Fact]
    public void SceneTargetsFlagMalformedActionsLeaveUnknownTypesEmptyAndRefuseBadTailStages()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Dialogue.esm", Build(1, Array.Empty<string>(), Top("SCEN",
            new Rec("SCEN", 0x800, Cat(ActionGroup(0, ("DATA", U32(0x810)), ("DMAX", U16(1))), ActionGroup(9, ("ZZZZ", U32(1))))),
            new Rec("SCEN", 0x801, ActionGroup(3, ("NPOT", U32(0x811)), ("NPOT", U32(0x812)))),
            new Rec("SCEN", 0x802, new[] { ("PNAM", U32(0x812)), ("SCQS", U16(1)) }),
            new Rec("SCEN", 0x803, Array.Empty<(string, byte[])>()))));
        using var env = RadicleEnvironment.Load(new[] { path });
        var scene = env.Scene(env.Winner(Key(0x800))!.Value);
        Assert.Null(scene.QuestStages);
        var dialogue = Assert.IsType<DialogueActionTarget>(scene.Actions[0].Target);
        Assert.True(dialogue.Malformed); Assert.Equal(Key(0x810), dialogue.Topic); Assert.Null(dialogue.LoopingMaxSeconds);
        Assert.Equal((ushort)9, scene.Actions[1].Type); Assert.Null(scene.Actions[1].Target);
        var repeated = Assert.IsType<PlayerDialogueActionTarget>(env.Scene(env.Winner(Key(0x801))!.Value).Actions[0].Target);
        Assert.True(repeated.Malformed); Assert.Equal(Key(0x811), repeated.NpcResponses.Positive);
        Assert.ThrowsAny<Exception>(() => env.Scene(env.Winner(Key(0x802))!.Value));
        Assert.Null(env.Scene(env.Winner(Key(0x803))!.Value).QuestStages);
    }

    [Fact]
    public void DialogueInfoResultReportsStagesPhaseAndEachFragmentState()
    {
        using var temp = new TempDirectory();
        var leading = U16(5).Concat(U16(2)).Concat(U16(0)).ToArray();
        var unsupported = U16(5).Concat(U16(2)).Concat(U16(1)).Concat(Wide("S")).Concat(new byte[] { 0 }).Concat(U16(1))
            .Concat(Wide("P")).Concat(new byte[] { 99, 0 }).ToArray();
        var path = temp.Write("Dialogue.esm", Build(1, Array.Empty<string>(), Top("INFO",
            new Rec("INFO", 0x800, new[] { ("TIQS", U16(3).Concat(U16(0xFFFF)).ToArray()), ("NAM0", Z("Phase_A")), ("VMAD", InfoVmad(2, (7, "Fragment_End"))) }),
            new Rec("INFO", 0x801, Array.Empty<(string, byte[])>()),
            new Rec("INFO", 0x802, new[] { ("VMAD", leading) }),
            new Rec("INFO", 0x803, new[] { ("VMAD", unsupported) }),
            new Rec("INFO", 0x804, new[] { ("TIQS", U16(57).Concat(U16(62)).ToArray()), ("VMAD", InfoVmad(3, (1, "Fragment_Begin"), (2, "Fragment_End"))) }),
            new Rec("INFO", 0x805, new[] { ("VMAD", InfoVmad(1)) }))));
        using var env = RadicleEnvironment.Load(new[] { path });
        DialogueInfoResult Read(uint id) => env.DialogueInfoResult(env.Winner(Key(id))!.Value);
        var endOnly = Read(0x800);
        Assert.Equal(InfoFragmentState.Present, endOnly.FragmentState); Assert.Null(endOnly.FragmentFailure);
        Assert.Equal((ushort)3, endOnly.OnBeginStage); Assert.Null(endOnly.OnEndStage); Assert.Equal("Phase_A", endOnly.StartScenePhase);
        Assert.Equal(3, endOnly.FragmentVersion); Assert.Equal((byte)2, endOnly.FragmentFlags);
        Assert.Equal(new InfoFragment(InfoFragmentKind.OnEnd, 7, "PF_Info", "Fragment_End"), Assert.Single(endOnly.Fragments));
        Assert.Equal("PF_Info", endOnly.FragmentScript!.Name); Assert.Equal("Caps", Assert.Single(endOnly.FragmentScript.Properties).Name);
        var none = Read(0x801);
        Assert.Equal(InfoFragmentState.NoVmad, none.FragmentState); Assert.Null(none.OnBeginStage); Assert.Null(none.StartScenePhase);
        Assert.Empty(none.Fragments); Assert.Null(none.FragmentScript);
        var bare = Read(0x802);
        Assert.Equal(InfoFragmentState.NoSection, bare.FragmentState); Assert.Empty(bare.Fragments); Assert.Null(bare.FragmentScript);
        var early = Read(0x803);
        Assert.Equal(InfoFragmentState.Unreadable, early.FragmentState); Assert.False(string.IsNullOrEmpty(early.FragmentFailure));
        Assert.Null(early.FragmentScript);
        var both = Read(0x804);
        Assert.Equal((ushort)57, both.OnBeginStage); Assert.Equal((ushort)62, both.OnEndStage);
        Assert.Equal(new[] { InfoFragmentKind.OnBegin, InfoFragmentKind.OnEnd }, both.Fragments.Select(f => f.Kind));
        Assert.Equal(new[] { "Fragment_Begin", "Fragment_End" }, both.Fragments.Select(f => f.Function));
        Assert.Equal(InfoFragmentState.Unreadable, Read(0x805).FragmentState);
    }

    [Fact]
    public void VanillaSceneStagesAndInfoFragmentsDecodeWhenDataIsAvailable()
    {
        var dataDir = Environment.GetEnvironmentVariable("FO4_DATA_DIR");
        if (string.IsNullOrEmpty(dataDir)) return;
        var master = Path.Combine(dataDir, "Fallout4.esm");
        if (!File.Exists(master)) return;
        using var env = RadicleEnvironment.Load(new[] { master });
        FormKey Vanilla(uint id) => new("Fallout4.esm", id);
        var staged = env.Winner(Vanilla(0x17B58));
        if (staged is null) return;
        Assert.Equal(((short)102, (short)91), env.Scene(staged.Value).QuestStages!.Value);
        Assert.Equal(((short)-1, (short)45), env.Scene(env.Winner(Vanilla(0x71407))!.Value).QuestStages!.Value);
        var begin = env.DialogueInfoResult(env.Winner(Vanilla(0x17D124))!.Value);
        Assert.Equal(InfoFragmentState.Present, begin.FragmentState);
        var row = Assert.Single(begin.Fragments);
        Assert.Equal(InfoFragmentKind.OnBegin, row.Kind); Assert.Equal("Fragment_Begin", row.Function);
        Assert.Equal("Caps001", Assert.Single(begin.FragmentScript!.Properties).Name);
        var both = env.DialogueInfoResult(env.Winner(Vanilla(0x34CF6))!.Value);
        Assert.Equal(2, both.Fragments.Count);
        Assert.Equal((ushort)57, env.DialogueInfoResult(env.Winner(Vanilla(0x3A487))!.Value).OnBeginStage);
        Assert.Equal("Barter", env.DialogueInfoResult(env.Winner(Vanilla(0x673E3))!.Value).StartScenePhase);
    }
}
