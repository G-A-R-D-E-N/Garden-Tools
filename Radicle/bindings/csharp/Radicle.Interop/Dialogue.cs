using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record DialogueText(NameKind Kind, string? Text, uint StringId);
public sealed record DialogueTopic(byte? Flags, byte? Category, ushort? Subtype, float? Priority, uint? InfoCount,
    byte[]? SubtypeName, FormKey? Branch, FormKey? Quest, FormKey? Keyword);
public sealed record DialogueCondition(byte FlagsByte, uint ComparisonRaw, ushort Function, ushort Unknown2,
    int Param1, int Param2, uint RunOn, uint Reference, int Unknown3, string? Parameter1String, string? Parameter2String);
public sealed record DialogueResponse(FormKey? Emotion, FormKey? Sound, byte Number, byte Unknown,
    ushort InterruptPercent, int CameraTargetAlias, int CameraLocationAlias, DialogueText Text,
    FormKey? SpeakerIdle, FormKey? ListenerIdle);
public sealed record DialogueInfo(ushort? Flags, ushort? ResetTimeEncoded, double? ResetHours, FormKey? PreviousTopic,
    FormKey? PreviousInfo, FormKey? SharedInfo, FormKey? InfoGroup, FormKey? Speaker, FormKey? StartScene,
    DialogueText Prompt, IReadOnlyList<DialogueResponse> Responses, IReadOnlyList<DialogueCondition> Conditions);
public sealed record ScenePhase(string? Name, uint? EditorWidth, ushort? Flags, (short Start, short Completion)? QuestStages,
    IReadOnlyList<DialogueCondition> StartConditions, IReadOnlyList<DialogueCondition> CompletionConditions);
public sealed record SceneActor(int Alias, uint Flags, uint BehaviorFlags);
public sealed record ScenePayload(string Signature, byte[] Bytes);
public sealed record SceneAction(ushort Type, string? Name, int? ActorAlias, uint? Index, uint? Flags,
    uint? StartPhase, uint? EndPhase, IReadOnlyList<ScenePayload> Payload);
public sealed record Scene(uint? Flags, FormKey? ParentQuest, IReadOnlyList<ScenePhase> Phases,
    IReadOnlyList<SceneActor> Actors, IReadOnlyList<SceneAction> Actions, IReadOnlyList<DialogueCondition> Conditions);

public sealed unsafe partial class RadicleEnvironment
{
    public DialogueTopic DialogueTopic(Record record)
    {
        using var use = Use(record);
        Native.DialogueTopicInfo t;
        Call.Check(RadicleNative.DialogueTopicGetInfo(use.Pointer, record.Handle, &t));
        byte[]? name = t.HasSubtypeName == 0 ? null : new[] { t.SubtypeName[0], t.SubtypeName[1], t.SubtypeName[2], t.SubtypeName[3] };
        return new(t.HasData == 0 ? null : t.Flags, t.HasData == 0 ? null : t.Category,
            t.HasData == 0 ? null : t.Subtype, t.HasPriority == 0 ? null : t.Priority,
            t.HasInfoCount == 0 ? null : t.InfoCount, name, FormKey.FromNative(t.Branch), FormKey.FromNative(t.Quest), FormKey.FromNative(t.Keyword));
    }

    private static DialogueText DialogueTextValue(Native.DialogueText t) => new(t.Kind, Utf8.Read(t.Text), t.StringId);

    private static DialogueCondition[] DialogueConditions(IntPtr pointer, nuint count) =>
        Call.Copy<Native.DialogueCondition>(pointer, count).Select(c => new DialogueCondition(c.FlagsByte,
            c.ComparisonRaw, c.Function, c.Unknown2, c.Param1, c.Param2, c.RunOn, c.Reference, c.Unknown3,
            Utf8.Read(c.Parameter1String), Utf8.Read(c.Parameter2String))).ToArray();

    public DialogueInfo DialogueInfo(Record record)
    {
        using var use = Use(record);
        Native.DialogueInfo* d;
        Call.Check(RadicleNative.DialogueInfoDecode(use.Pointer, record.Handle, &d));
        try
        {
            var responses = Call.Copy<Native.DialogueResponse>(d->Responses, d->ResponseCount).Select(r =>
                new DialogueResponse(FormKey.FromNative(r.Emotion), FormKey.FromNative(r.Sound), r.Number, r.Unknown,
                    r.InterruptPercent, r.CameraTargetAlias, r.CameraLocationAlias, DialogueTextValue(r.Text),
                    FormKey.FromNative(r.SpeakerIdle), FormKey.FromNative(r.ListenerIdle))).ToArray();
            return new(d->HasData == 0 ? null : d->Flags, d->HasData == 0 ? null : d->ResetTimeEncoded,
                d->HasResetHours == 0 ? null : d->ResetHours,
                FormKey.FromNative(d->PreviousTopic), FormKey.FromNative(d->PreviousInfo), FormKey.FromNative(d->SharedInfo),
                FormKey.FromNative(d->InfoGroup), FormKey.FromNative(d->Speaker), FormKey.FromNative(d->StartScene),
                DialogueTextValue(d->Prompt), responses, DialogueConditions(d->Conditions, d->ConditionCount));
        }
        finally { RadicleNative.DialogueInfoFree(d); }
    }

    private static ScenePayload ScenePayloadValue(Native.ScenePayload p) => new(Signature(p.Signature), Utf8.Raw(p.Bytes, p.Size));

    public Scene Scene(Record record)
    {
        using var use = Use(record);
        Native.SceneInfo* s;
        Call.Check(RadicleNative.SceneDecode(use.Pointer, record.Handle, &s));
        try
        {
            var phases = Call.Copy<Native.ScenePhase>(s->Phases, s->PhaseCount).Select(p => new ScenePhase(Utf8.Read(p.Name),
                p.HasEditorWidth == 0 ? null : p.EditorWidth, p.HasFlags == 0 ? null : p.Flags,
                p.HasQuestStages == 0 ? null : (p.StartQuestStage, p.CompletionQuestStage),
                DialogueConditions(p.StartConditions, p.StartConditionCount),
                DialogueConditions(p.CompletionConditions, p.CompletionConditionCount))).ToArray();
            var actors = Call.Copy<Native.SceneActor>(s->Actors, s->ActorCount).Select(a => new SceneActor(a.Alias, a.Flags, a.BehaviorFlags)).ToArray();
            var actions = Call.Copy<Native.SceneAction>(s->Actions, s->ActionCount).Select(a => new SceneAction(a.Type, Utf8.Read(a.Name),
                a.HasActorAlias == 0 ? null : a.ActorAlias, a.HasIndex == 0 ? null : a.Index, a.HasFlags == 0 ? null : a.Flags,
                a.HasStartPhase == 0 ? null : a.StartPhase, a.HasEndPhase == 0 ? null : a.EndPhase,
                Call.Copy<Native.ScenePayload>(a.Payload, a.PayloadCount).Select(ScenePayloadValue).ToArray())).ToArray();
            return new(s->HasFlags == 0 ? null : s->Flags, FormKey.FromNative(s->ParentQuest), phases, actors, actions,
                DialogueConditions(s->Conditions, s->ConditionCount));
        }
        finally { RadicleNative.SceneFree(s); }
    }

    public static string SceneActionTypeName(ushort type) => Utf8.Read(RadicleNative.SceneActionTypeName(type)) ?? string.Empty;
}
