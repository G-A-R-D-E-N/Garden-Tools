using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class DialogueAuthoringTests
{
    private const string Plugin = "Dlg.esm";

    private static Radicle.Interop.FormKey K(uint id) => new(Plugin, id);

    private static readonly Radicle.Interop.FormKey Quest = K(0x800), Topic = K(0x801), Info = K(0x802), Scene = K(0x803),
        Group = K(0x804), Bare = K(0x806), Keyword = K(0x821), Sound = K(0x822), Misc = K(0x828);

    private static byte[] U16(params ushort[] values) => values.SelectMany(BitConverter.GetBytes).ToArray();

    private static byte[] Response(uint emotion, byte number, uint sound) =>
        U32(emotion).Concat(new[] { number }).Concat(U32(sound)).Concat(new byte[] { 0 }).Concat(U16(50))
            .Concat(U32(0xFFFFFFFF, 0xFFFFFFFF)).ToArray();

    private static byte[] Source()
    {
        var empty = Array.Empty<byte>();
        var topic = new Rec("DIAL", 0x801, new[] { ("EDID", Z("Topic")), ("FULL", Z("Hello")), ("PNAM", F32(50f)),
            ("QNAM", U32(0x800)), ("DATA", U32(0x000E0201)), ("SNAM", U32(0x4E454353)), ("TIFC", U32(1)) });
        var info = new Rec("INFO", 0x802, new[] { ("ENAM", U16(1, 0)), ("TRDA", Response(0x821, 1, 0)), ("NAM1", Z("Line")),
            ("NAM2", Z("")), ("NAM3", Z("")), ("NAM4", Z("")), ("TRDA", Response(0xFFFFFFFF, 2, 0x822)), ("NAM1", Z("Two")),
            ("NAM2", Z("")), ("NAM3", Z("")), ("NAM4", Z("")), ("NAM0", Z("Start")), ("INAM", U32(1)) });
        var group = new Rec("INFO", 0x804, new[] { ("ENAM", U16(0x40, 0)), ("INAM", U32(1)) });
        var scene = new Rec("SCEN", 0x803, new[] { ("EDID", Z("Scene")), ("FNAM", U32(0x24)),
            ("HNAM", empty), ("NAM0", Z("")), ("NEXT", empty), ("NEXT", empty), ("WNAM", U32(350)), ("HNAM", empty),
            ("ALID", U32(0)), ("LNAM", U32(0)), ("DNAM", U32(26)),
            ("ANAM", U16(0)), ("NAM0", Z("")), ("ALID", U32(0)), ("INAM", U32(1)), ("SNAM", U32(0)), ("ENAM", U32(0)),
            ("DATA", U32(0x801)), ("ANAM", empty),
            ("ANAM", U16(2)), ("NAM0", Z("")), ("ALID", U32(0)), ("INAM", U32(2)), ("SNAM", U32(0)), ("SNAM", F32(5f)),
            ("ANAM", empty),
            ("PNAM", U32(0x800)), ("INAM", U32(2)), ("VNAM", U32(3, 3, 3, 3)), ("NNAM", Z("note")), ("XNAM", U32(0)) });
        var bare = new Rec("DIAL", 0x806, new[] { ("EDID", Z("Bare")), ("PNAM", F32(50f)) });
        var edid = ("EDID", Z("Other"));
        return Build(1, Array.Empty<string>(),
            Top("QUST", new Rec("QUST", 0x800, new[] { edid }),
                new Grp(0x800, 10, new Node[] { topic, new Grp(0x801, 7, new Node[] { info, group }), scene, bare })),
            Top("KYWD", new Rec("KYWD", 0x821, new[] { edid })),
            Top("SNDR", new Rec("SNDR", 0x822, new[] { edid })),
            Top("MISC", new Rec("MISC", 0x828, new[] { edid })));
    }

    private static Status Refusal(Action action) => Assert.Throws<RadicleException>(action).Status;

    [Theory]
    [InlineData("DIAL", "BNAM", "DIAL\\Branch", "DLBR")]
    [InlineData("DIAL", "QNAM", "DIAL\\Quest", "QUST")]
    [InlineData("DIAL", "KNAM", "DIAL\\Keyword", "KYWD")]
    [InlineData("INFO", "TPIC", "INFO\\PreviousTopic", "DIAL")]
    [InlineData("INFO", "PNAM", "INFO\\PreviousInfo", "INFO")]
    [InlineData("INFO", "DNAM", "INFO\\SharedInfo", "INFO")]
    [InlineData("INFO", "GNAM", "INFO\\InfoGroup", "INFO")]
    [InlineData("INFO", "ANAM", "INFO\\Speaker", "NPC_")]
    [InlineData("INFO", "TSCE", "INFO\\StartScene", "SCEN")]
    [InlineData("INFO", "ONAM", "INFO\\AudioOutputOverride", "SOPM")]
    [InlineData("INFO", "MODQ", "INFO\\ResetGlobal", "GLOB")]
    public void MalformedTopLevelLinksRefuseWithoutChangingBytes(string record, string subrecord, string field, string target)
    {
        foreach (var size in new[] { 0, 3, 5, 8 })
        {
            using var temp = new TempDirectory();
            var source = Build(1, Array.Empty<string>(),
                Top(record, new Rec(record, 0x800, new[] { (subrecord, Enumerable.Repeat((byte)0x55, size).ToArray()) })),
                Top(target, new Rec(target, 0x801, new[] { ("EDID", Z("Target")) })));
            using var plugin = PluginFile.Read(temp.Write(Plugin, source));
            var before = plugin.ToBytes();
            Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetFormLink(K(0x800), field, K(0x801))));
            Assert.Equal(before, plugin.ToBytes());
        }
    }

    [Fact]
    public void TopicAndInfoFieldsAndAuthoringSurviveReload()
    {
        using var temp = new TempDirectory();
        var path = temp.Write(Plugin, Source());
        using var plugin = PluginFile.Read(path);

        Assert.True(plugin.SetScalar(Topic, "DIAL\\Priority", "65"));
        Assert.True(plugin.SetScalar(Topic, "DIAL\\Category", "3"));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetScalar(Topic, "DIAL\\Category", "8")));
        Assert.True(plugin.SetString(Topic, "DIAL\\Name", "Greeting"));
        Assert.False(plugin.SetFormLink(Topic, "DIAL\\Keyword", Keyword));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetFormLink(Topic, "DIAL\\Quest", Misc)));
        Assert.Equal(2, plugin.FieldCount(Info, "INFO\\SoundFile"));
        Assert.True(plugin.SetFormLink(Info, "INFO\\SoundFile", Sound, 0));
        Assert.True(plugin.SetFormLink(Info, "INFO\\SoundFile", null, 1));
        Assert.True(plugin.SetFormLink(Info, "INFO\\Emotion", Keyword, 1));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetFormLink(Info, "INFO\\Emotion", null, 0)));
        Assert.True(plugin.SetScalar(Info, "INFO\\CameraTargetAlias", "4", 1));
        Assert.True(plugin.SetString(Info, "INFO\\ResponseText", "Changed", 1));
        Assert.True(plugin.SetString(Info, "INFO\\StartScenePhase", "Next"));
        Assert.True(plugin.AddSubrecord(Bare, "DATA"));
        Assert.True(plugin.AddSubrecord(Group, "IOVR"));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.AddSubrecord(Info, "IOVR")));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.AddSubrecord(Bare, "QNAM")));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.RemoveSubrecord(Bare, "PNAM")));
        Assert.True(plugin.RemoveSubrecord(Topic, "FULL"));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.CreateRecord("DIAL", "NewTopic")));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.CreateRecord("INFO", "NewInfo")));
        var edited = plugin.ToBytes();
        plugin.Write(path);

        using var env = RadicleEnvironment.Load(new[] { path });
        var topic = env.DialogueTopic(env.Winner(Topic)!.Value);
        Assert.Equal(65f, topic.Priority);
        Assert.Equal((byte)3, topic.Category);
        Assert.Equal((byte)0, env.DialogueTopic(env.Winner(Bare)!.Value).Category);
        var info = env.DialogueInfo(env.Winner(Info)!.Value);
        Assert.Equal(Sound, info.Responses[0].Sound);
        Assert.Null(info.Responses[1].Sound);
        Assert.Equal(Keyword, info.Responses[1].Emotion);
        Assert.Equal(4, info.Responses[1].CameraTargetAlias);
        Assert.Equal("Changed", info.Responses[1].Text.Text);
        using var reloaded = PluginFile.Read(path);
        Assert.Equal(edited, reloaded.ToBytes());
    }

    [Fact]
    public void SceneTypedWriterUsesExplicitGroupIndexes()
    {
        using var temp = new TempDirectory();
        var path = temp.Write(Plugin, Source());
        using var plugin = PluginFile.Read(path);

        Assert.Equal(1, plugin.SceneGroupCount(Scene, ScenePart.Phase));
        Assert.Equal(1, plugin.SceneGroupCount(Scene, ScenePart.Actor));
        Assert.Equal(2, plugin.SceneGroupCount(Scene, ScenePart.Action));
        Assert.True(plugin.SetSceneScalar(Scene, "SCEN\\Action\\EndPhase", "1", 0));
        Assert.False(plugin.SetSceneScalar(Scene, "SCEN\\Action\\EndPhase", "1", 1));
        Assert.True(plugin.SetSceneScalar(Scene, "SCEN\\Action\\StartPhase", "1", 1));
        Assert.True(plugin.SetSceneScalar(Scene, "SCEN\\Actor\\BehaviorFlags", "10"));
        Assert.True(plugin.SetSceneScalar(Scene, "SCEN\\Phase\\EditorWidth", "500"));
        Assert.True(plugin.SetSceneScalar(Scene, "SCEN\\Flags", "4"));
        Assert.True(plugin.SetSceneScalar(Scene, "SCEN\\LastActionIndex", "3"));
        Assert.False(plugin.SetSceneScalar(Scene, "SCEN\\Phase\\Flags", "1", 0));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetSceneScalar(Scene, "SCEN\\Phase\\EditorWidth", "-1", 0)));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetSceneScalar(Topic, "SCEN\\Flags", "1")));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetSceneFormLink(Scene, "SCEN\\ParentQuest", Keyword)));
        Assert.True(plugin.SetSceneFormLink(Scene, "SCEN\\ParentQuest", Quest));
        Assert.False(plugin.SetSceneFormLink(Scene, "SCEN\\TemplateScene", Scene));
        Assert.True(plugin.SetScalar(Scene, "SCEN\\CombatBehavior", "1"));
        Assert.True(plugin.RemoveSubrecord(Scene, "XNAM"));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.RemoveSubrecord(Scene, "FNAM")));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.CreateRecord("SCEN", "NewScene")));
        plugin.Write(path);

        using var env = RadicleEnvironment.Load(new[] { path });
        var scene = env.Scene(env.Winner(Scene)!.Value);
        Assert.Equal(4u, scene.Flags);
        Assert.Equal(500u, scene.Phases[0].EditorWidth);
        Assert.Equal(10u, scene.Actors[0].BehaviorFlags);
        Assert.Equal(1u, scene.Actions[0].EndPhase);
        Assert.Equal(1u, scene.Actions[1].StartPhase);
        Assert.Null(scene.Actions[1].EndPhase);
        Assert.Equal("SNAM", Assert.Single(scene.Actions[1].Payload).Signature);
        Assert.Equal(Quest, scene.ParentQuest);
    }

    [Fact]
    public void SceneGroupCountOfAMissingRecordThrowsKeyNotFoundAndWrongTypeIsRefused()
    {
        using var temp = new TempDirectory();
        var path = temp.Write(Plugin, Source());
        using var plugin = PluginFile.Read(path);

        Assert.Throws<KeyNotFoundException>(() => plugin.SceneGroupCount(K(0x8FF), ScenePart.Actor));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SceneGroupCount(Topic, ScenePart.Actor)));
    }
}
