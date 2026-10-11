using Godot;
using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using Radicle.Interop;

namespace OpenCommonwealth;

public partial class DialogueProbe : Node
{
    public override void _Ready()
    {
        var data = System.Environment.GetEnvironmentVariable("FO4_DATA_DIR");
        if (string.IsNullOrEmpty(data))
        {
            GD.Print("dialogue_probe SKIP: FO4_DATA_DIR unset");
            GetTree().Quit(77);
            return;
        }
        try
        {
            using var env = RadicleEnvironment.Load(new[] { Path.Combine(data, "Fallout4.esm") });
            var topic = env.WinnersOf("DIAL")
                .Where(t => IsRecordOf(env, env.DialogueTopic(t).Quest, "QUST") && env.TopicChildren(t).Count > 0)
                .Cast<Record?>().FirstOrDefault() ?? throw new InvalidOperationException("no quest-linked DIAL with INFO children");
            var questKey = env.DialogueTopic(topic).Quest;
            var (infoRecord, info) = env.TopicChildren(topic).Select(r => (Entry: r, Info: env.DialogueInfo(r)))
                .FirstOrDefault(x => x.Info.Responses.Count > 0 && LinksResolve(env, x.Info.PreviousTopic, x.Info.PreviousInfo,
                    x.Info.SharedInfo, x.Info.InfoGroup, x.Info.Speaker, x.Info.StartScene));
            if (info is null) throw new InvalidOperationException("no INFO under the topic has resolvable links and a response");
            using var tables = StringTables.Load(env, Path.Combine(data, "Strings"));
            var text = tables.ResolveDialogueText(infoRecord, "NAM1", info.Responses[0].Text);
            if (text is null || text.Length == 0) throw new InvalidOperationException("INFO response text unresolved in Strings");
            var scene = env.WinnersOf("SCEN")
                .Where(s => IsRecordOf(env, env.Scene(s).ParentQuest, "QUST"))
                .Cast<Record?>().FirstOrDefault() ?? throw new InvalidOperationException("no SCEN with a resolvable quest link");
            GD.Print("dialogue_probe PASS " + JsonSerializer.Serialize(new
            {
                Topic = env.Info(topic).FormKey.ToString(), Quest = questKey?.ToString(), TopicInfos = env.TopicChildren(topic).Count,
                Info = env.Info(infoRecord).FormKey.ToString(), ResponseTextBytes = text.Length,
                Scene = env.Info(scene).FormKey.ToString(), SceneQuest = env.Scene(scene).ParentQuest?.ToString(),
            }));
            GetTree().Quit(0);
        }
        catch (Exception error)
        {
            GD.PrintErr("dialogue_probe FAIL: " + error);
            GetTree().Quit(1);
        }
    }

    private static bool IsRecordOf(RadicleEnvironment env, FormKey? key, string signature) =>
        key is { } k && env.Winner(k) is { } record && env.Info(record).Signature == signature;

    private static bool LinksResolve(RadicleEnvironment env, params FormKey?[] links) =>
        links.Any(link => link is not null) && links.All(link => link is not { } key || env.Winner(key) is not null);
}
