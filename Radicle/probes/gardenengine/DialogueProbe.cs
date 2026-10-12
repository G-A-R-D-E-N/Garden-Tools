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
            var archives = new[] { Path.Combine(data, "Fallout4 - Interface.ba2") }.Where(File.Exists).ToArray();
            using var assets = AssetResolver.Open(data, archives);
            using var tables = StringTables.Load(env, Path.Combine(data, "Strings"), "en", assets);
            var (topic, infoRecord, info, text) = env.WinnersOf("DIAL")
                .Where(t => IsRecordOf(env, env.DialogueTopic(t).Quest, "QUST"))
                .SelectMany(t => env.TopicChildren(t).Select(r => (Topic: t, Entry: r, Info: env.DialogueInfo(r))))
                .Where(x => LinksResolve(env, x.Info.PreviousTopic, x.Info.PreviousInfo,
                    x.Info.SharedInfo, x.Info.InfoGroup, x.Info.Speaker, x.Info.StartScene))
                .Select(x => (x.Topic, x.Entry, x.Info, Text: ResponseText(tables, x.Entry, x.Info)))
                .FirstOrDefault(x => x.Text is not null);
            if (text is null) throw new InvalidOperationException("no INFO in Fallout4.esm has resolvable links and a non-empty response text");
            var questKey = env.DialogueTopic(topic).Quest;
            var entries = env.TopicChildren(topic);
            if (entries.Count == 0) throw new InvalidOperationException("selected DIAL has no INFO children");
            var previousFollowed = 0;
            var responsesResolved = 0;
            foreach (var entry in entries)
            {
                var entryInfo = env.DialogueInfo(entry);
                if (entryInfo.PreviousInfo is { } previous && env.Winner(previous) is not null) ++previousFollowed;
                responsesResolved += entryInfo.Responses.Count(response => tables.ResolveDialogueText(entry, "NAM1", response.Text) is not null);
            }
            var (scene, sceneValue) = env.WinnersOf("SCEN")
                .Select(s => (Entry: s, Value: env.Scene(s)))
                .FirstOrDefault(x => IsRecordOf(env, x.Value.ParentQuest, "QUST") && x.Value.Phases.Count > 0 && x.Value.Actions.Count > 0);
            if (sceneValue is null) throw new InvalidOperationException("no SCEN with a resolvable quest link, a phase and an action");
            GD.Print("dialogue_probe PASS " + JsonSerializer.Serialize(new
            {
                Topic = env.Info(topic).FormKey.ToString(), Quest = questKey?.ToString(), Infos = entries.Count,
                ResponsesResolved = responsesResolved, PreviousInfoFollowed = previousFollowed,
                Info = env.Info(infoRecord).FormKey.ToString(), ResponseTextBytes = text.Length,
                Scene = env.Info(scene).FormKey.ToString(), SceneQuest = sceneValue.ParentQuest?.ToString(),
                ScenePhases = sceneValue.Phases.Count, SceneActions = sceneValue.Actions.Count,
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
        links.All(link => link is not { } key || env.Winner(key) is not null);

    private static byte[]? ResponseText(StringTables tables, Record record, DialogueInfo info) =>
        info.Responses.Select(response => tables.ResolveDialogueText(record, "NAM1", response.Text))
            .FirstOrDefault(text => text is { Length: > 0 });
}
