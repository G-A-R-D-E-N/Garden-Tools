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
}
