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
    uint? StartPhase, uint? EndPhase, IReadOnlyList<ScenePayload> Payload)
{
    public SceneActionTarget? Target { get; init; }
    public SceneActionFlags? ActionFlags => Flags is { } flags ? (SceneActionFlags)flags : null;
}
public sealed record SceneStartSceneEntry(FormKey? Scene, ushort PhaseIndex, string? PhaseName,
    IReadOnlyList<DialogueCondition> Conditions);
public sealed record Scene(uint? Flags, FormKey? ParentQuest, IReadOnlyList<ScenePhase> Phases,
    IReadOnlyList<SceneActor> Actors, IReadOnlyList<SceneAction> Actions, IReadOnlyList<DialogueCondition> Conditions)
{
    public (short OnBegin, short OnEnd)? QuestStages { get; init; }
}

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
            var targets = SceneTargetValues(use.Pointer, record.Handle);
            if (targets.Actions.Length != checked((int)s->ActionCount)) throw new InvalidOperationException("Radicle returned a different scene action count for the targets.");
            var actions = Call.Copy<Native.SceneAction>(s->Actions, s->ActionCount).Select((a, index) => new SceneAction(a.Type, Utf8.Read(a.Name),
                a.HasActorAlias == 0 ? null : a.ActorAlias, a.HasIndex == 0 ? null : a.Index, a.HasFlags == 0 ? null : a.Flags,
                a.HasStartPhase == 0 ? null : a.StartPhase, a.HasEndPhase == 0 ? null : a.EndPhase,
                Call.Copy<Native.ScenePayload>(a.Payload, a.PayloadCount).Select(ScenePayloadValue).ToArray()) { Target = targets.Actions[index] }).ToArray();
            return new(s->HasFlags == 0 ? null : s->Flags, FormKey.FromNative(s->ParentQuest), phases, actors, actions,
                DialogueConditions(s->Conditions, s->ConditionCount)) { QuestStages = targets.QuestStages };
        }
        finally { RadicleNative.SceneFree(s); }
    }

    public static string SceneActionTypeName(ushort type) => Utf8.Read(RadicleNative.SceneActionTypeName(type)) ?? string.Empty;
}

public enum SceneActionType : ushort
{
    Dialogue = 0,
    Package = 1,
    Timer = 2,
    PlayerDialogue = 3,
    StartScene = 4,
    NpcResponseDialogue = 5,
    Radio = 6
}

public enum SceneEmotionType : uint
{
    Neutral = 0,
    Anger = 1,
    Disgust = 2,
    Fear = 3,
    Sad = 4,
    Happy = 5,
    Surprise = 6,
    Puzzled = 7
}

[Flags]
public enum SceneActionFlags : uint
{
    None = 0,
    PlayerPositiveUseDialogueSubtype = 1u << 7,
    HoldIntoNextScene = 1u << 7,
    PlayerNegativeUseDialogueSubtype = 1u << 8,
    PlayerNeutralUseDialogueSubtype = 1u << 9,
    UseDialogueSubtype = 1u << 10,
    PlayerQuestionUseDialogueSubtype = 1u << 11,
    KeepClearTargetOnActionEnd = 1u << 12,
    RunOnEndOfPhase = 1u << 14,
    FaceTarget = 1u << 15,
    Looping = 1u << 16,
    HeadtrackPlayer = 1u << 17,
    IgnoreForCompletion = 1u << 19,
    CameraSpeakerTarget = 1u << 21,
    CompleteFaceTarget = 1u << 22,
    NpcPositiveUseDialogueSubtype = 1u << 27,
    NpcNegativeUseDialogueSubtype = 1u << 28,
    NpcNeutralUseDialogueSubtype = 1u << 29,
    NpcQuestionUseDialogueSubtype = 1u << 30
}

public enum InfoFragmentKind : byte
{
    OnBegin = 1,
    OnEnd = 2
}

public sealed record SceneReplyLinks(FormKey? Positive, FormKey? Negative, FormKey? Neutral, FormKey? Question);

// Constraint: Malformed is true when a known action type held a wrong size, a repeated single field or an unlisted signature; every field that did decode is still set. An unknown action type has no target.
public abstract record SceneActionTarget(bool Malformed);

public sealed record DialogueActionTarget(bool Malformed, FormKey? Topic, float? LoopingMaxSeconds, float? LoopingMinSeconds,
    (float Fov, float Rate)? Camera, SceneEmotionType? EmotionType, uint? EmotionValue,
    IReadOnlyList<int> HeadtrackingAliases, FormKey? DialogueSubtype, FormKey? AudioOutputOverride,
    FormKey? AnimArchType) : SceneActionTarget(Malformed);

public sealed record PackageActionTarget(bool Malformed, IReadOnlyList<FormKey> Packages) : SceneActionTarget(Malformed);

public sealed record TimerActionTarget(bool Malformed, float? MaxSeconds, float? MinSeconds, short? QuestStage)
    : SceneActionTarget(Malformed);

public sealed record PlayerDialogueActionTarget(bool Malformed, SceneReplyLinks PlayerResponses,
    SceneReplyLinks PlayerSubtypes, SceneReplyLinks NpcResponses, SceneReplyLinks NpcSubtypes,
    IReadOnlyList<int> NpcHeadtrackingAliases, FormKey? AudioOutputOverride, int? DialogueTargetActor)
    : SceneActionTarget(Malformed);

public sealed record NpcResponseActionTarget(bool Malformed, SceneReplyLinks PlayerResponses,
    SceneReplyLinks PlayerSubtypes, IReadOnlyList<int> NpcHeadtrackingAliases, FormKey? AudioOutputOverride)
    : SceneActionTarget(Malformed);

public sealed record StartSceneActionTarget(bool Malformed, FormKey? Topic, IReadOnlyList<SceneStartSceneEntry> Entries,
    bool EndSceneSayGreeting) : SceneActionTarget(Malformed);

public sealed record RadioActionTarget(bool Malformed, FormKey? Topic, FormKey? Sound, float? LoopingMaxSeconds,
    FormKey? DialogueSubtype) : SceneActionTarget(Malformed);

public sealed record InfoFragment(InfoFragmentKind Kind, byte Unknown, string Script, string Function);

// Constraint: FragmentState tells no VMAD, VMAD without a fragment section and an unreadable section apart; only Present carries Fragments and FragmentScript.
// Constraint: OnBeginStage and OnEndStage are null when the INFO has no TIQS or the stored value is 0xFFFF (unset).
public sealed record DialogueInfoResult(ushort? OnBeginStage, ushort? OnEndStage, string? StartScenePhase,
    InfoFragmentState FragmentState, string? FragmentFailure, int FragmentVersion, byte FragmentFlags,
    IReadOnlyList<InfoFragment> Fragments, VmadScriptInfo? FragmentScript);

// Constraint: Index is the stored 32-bit Fragment Index; that it equals a terminal menu item id is UNVERIFIED, so the caller joins it to the menu item id itself and treats a miss as no fragment.
public sealed record TerminalFragment(uint Index, byte Unknown, string Script, string Function);

// Constraint: FragmentState tells no VMAD, VMAD without a fragment section and an unreadable section apart; only Present carries FragmentScriptName, Fragments and FragmentScript. Fragments keep stored order, so look one up by Index and never by position.
public sealed record TerminalFragmentsResult(TermFragmentState FragmentState, string? FragmentFailure, int FragmentVersion,
    string? FragmentScriptName, IReadOnlyList<TerminalFragment> Fragments, VmadScriptInfo? FragmentScript);

public sealed unsafe partial class RadicleEnvironment
{
    private static FormKey? SceneLink(Native.SceneTargetLink[] links, Native.SceneLinkSlot slot)
    {
        foreach (var link in links) if (link.Slot == slot) return FormKey.FromNative(link.Key);
        return null;
    }

    private static SceneActionTarget? SceneActionTargetValue(Native.SceneActionTarget t)
    {
        var links = Call.Copy<Native.SceneTargetLink>(t.Links, t.LinkCount);
        var heads = Call.Copy<int>(t.HeadTracking, t.HeadTrackingCount);
        var malformed = t.Malformed != 0;
        float? max = t.HasMaxSeconds == 0 ? null : t.MaxSeconds;
        float? min = t.HasMinSeconds == 0 ? null : t.MinSeconds;
        FormKey? Link(Native.SceneLinkSlot slot) => SceneLink(links, slot);
        SceneReplyLinks Player(Native.SceneLinkSlot a, Native.SceneLinkSlot b, Native.SceneLinkSlot c, Native.SceneLinkSlot d) =>
            new(Link(a), Link(b), Link(c), Link(d));
        var playerResponses = Player(Native.SceneLinkSlot.PlayerPositiveResponse, Native.SceneLinkSlot.PlayerNegativeResponse,
            Native.SceneLinkSlot.PlayerNeutralResponse, Native.SceneLinkSlot.PlayerQuestionResponse);
        var playerSubtypes = Player(Native.SceneLinkSlot.PlayerPositiveSubtype, Native.SceneLinkSlot.PlayerNegativeSubtype,
            Native.SceneLinkSlot.PlayerNeutralSubtype, Native.SceneLinkSlot.PlayerQuestionSubtype);
        switch ((SceneActionType)t.Type)
        {
            case SceneActionType.Dialogue:
                (float Fov, float Rate)? camera = t.HasCamera == 0 ? null : (t.CameraFov, t.CameraRate);
                SceneEmotionType? emotion = t.HasEmotionType == 0 ? null : (SceneEmotionType)t.EmotionType;
                uint? emotionValue = t.HasEmotionValue == 0 ? null : t.EmotionValue;
                return new DialogueActionTarget(malformed, Link(Native.SceneLinkSlot.Topic), max, min, camera, emotion,
                    emotionValue, heads, Link(Native.SceneLinkSlot.DialogueSubtype),
                    Link(Native.SceneLinkSlot.AudioOutputOverride), Link(Native.SceneLinkSlot.AnimArchType));
            case SceneActionType.Package:
                var packages = Call.Copy<Native.FormKey>(t.Packages, t.PackageCount)
                    .Select(key => FormKey.FromNative(key)).Where(key => key.HasValue).Select(key => key!.Value).ToArray();
                return new PackageActionTarget(malformed, packages);
            case SceneActionType.Timer:
                short? stage = t.HasQuestStage == 0 ? null : t.QuestStage;
                return new TimerActionTarget(malformed, max, min, stage);
            case SceneActionType.PlayerDialogue:
                int? target = t.HasDialogueTargetActor == 0 ? null : t.DialogueTargetActor;
                return new PlayerDialogueActionTarget(malformed, playerResponses, playerSubtypes,
                    Player(Native.SceneLinkSlot.NpcPositiveResponse, Native.SceneLinkSlot.NpcNegativeResponse,
                        Native.SceneLinkSlot.NpcNeutralResponse, Native.SceneLinkSlot.NpcQuestionResponse),
                    Player(Native.SceneLinkSlot.NpcPositiveSubtype, Native.SceneLinkSlot.NpcNegativeSubtype,
                        Native.SceneLinkSlot.NpcNeutralSubtype, Native.SceneLinkSlot.NpcQuestionSubtype),
                    heads, Link(Native.SceneLinkSlot.AudioOutputOverride), target);
            case SceneActionType.NpcResponseDialogue:
                return new NpcResponseActionTarget(malformed, playerResponses, playerSubtypes, heads,
                    Link(Native.SceneLinkSlot.AudioOutputOverride));
            case SceneActionType.StartScene:
                var entries = Call.Copy<Native.SceneStartEntry>(t.StartEntries, t.StartEntryCount).Select(e =>
                    new SceneStartSceneEntry(FormKey.FromNative(e.Scene), e.PhaseIndex,
                        e.HasPhaseName == 0 ? null : Utf8.Read(e.PhaseName),
                        DialogueConditions(e.Conditions, e.ConditionCount))).ToArray();
                return new StartSceneActionTarget(malformed, Link(Native.SceneLinkSlot.Topic), entries, t.EndSceneGreeting != 0);
            case SceneActionType.Radio:
                return new RadioActionTarget(malformed, Link(Native.SceneLinkSlot.Topic), Link(Native.SceneLinkSlot.Sound),
                    max, Link(Native.SceneLinkSlot.DialogueSubtype));
            default:
                return null;
        }
    }

    private static ((short OnBegin, short OnEnd)? QuestStages, SceneActionTarget?[] Actions) SceneTargetValues(IntPtr env, IntPtr record)
    {
        Native.SceneTargets* t;
        Call.Check(RadicleNative.SceneTargetsDecode(env, record, &t));
        try
        {
            (short OnBegin, short OnEnd)? stages = t->HasQuestStages == 0 ? null : (t->QuestBeginStage, t->QuestEndStage);
            var actions = Call.Copy<Native.SceneActionTarget>(t->Actions, t->ActionCount).Select(SceneActionTargetValue).ToArray();
            return (stages, actions);
        }
        finally { RadicleNative.SceneTargetsFree(t); }
    }

    public DialogueInfoResult DialogueInfoResult(Record record)
    {
        using var use = Use(record);
        Native.InfoResult* r;
        Call.Check(RadicleNative.InfoResultDecode(use.Pointer, record.Handle, &r));
        try
        {
            var rows = Call.Copy<Native.InfoFragmentRow>(r->Rows, r->RowCount).Select(row => new InfoFragment(
                (InfoFragmentKind)row.Kind, row.Unknown, Utf8.Read(row.Script) ?? string.Empty,
                Utf8.Read(row.Function) ?? string.Empty)).ToArray();
            var script = r->FragmentState == InfoFragmentState.Present ? InfoFragmentScript(use.Pointer, record.Handle) : null;
            ushort? begin = r->HasStages == 0 || r->BeginStage == 0xFFFF ? null : r->BeginStage;
            ushort? end = r->HasStages == 0 || r->EndStage == 0xFFFF ? null : r->EndStage;
            return new(begin, end, r->HasStartScenePhase == 0 ? null : Utf8.Read(r->StartScenePhase), r->FragmentState,
                Utf8.Read(r->FragmentFailure), r->FragmentVersion, r->FragmentFlags, rows, script);
        }
        finally { RadicleNative.InfoResultFree(r); }
    }

    // Constraint: a record that is not TERM is refused with RadicleException; the PERK fragment layout is the same and must not be read as a terminal.
    public TerminalFragmentsResult TerminalFragments(Record record)
    {
        using var use = Use(record);
        Native.TermFragments* r;
        Call.Check(RadicleNative.TermFragmentsDecode(use.Pointer, record.Handle, &r));
        try
        {
            var rows = Call.Copy<Native.TermFragmentRow>(r->Rows, r->RowCount).Select(row => new TerminalFragment(
                row.Index, row.Unknown, Utf8.Read(row.Script) ?? string.Empty, Utf8.Read(row.Function) ?? string.Empty)).ToArray();
            var script = r->State == TermFragmentState.Present ? TerminalFragmentScript(use.Pointer, record.Handle) : null;
            return new(r->State, Utf8.Read(r->Failure), r->Version, Utf8.Read(r->ScriptName), rows, script);
        }
        finally { RadicleNative.TermFragmentsFree(r); }
    }

    private static VmadScriptInfo TerminalFragmentScript(IntPtr env, IntPtr record)
    {
        IntPtr handle;
        Call.Check(RadicleNative.TermFragmentScript(env, record, &handle));
        try
        {
            return VmadScriptReader.ReadScript(handle, 0);
        }
        finally { RadicleNative.VmadScriptsDestroy(handle); }
    }

    private static VmadScriptInfo InfoFragmentScript(IntPtr env, IntPtr record)
    {
        IntPtr handle;
        Call.Check(RadicleNative.InfoFragmentScript(env, record, &handle));
        try
        {
            return VmadScriptReader.ReadScript(handle, 0);
        }
        finally { RadicleNative.VmadScriptsDestroy(handle); }
    }
}
