using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record DialogueBranchDraft(string? EditorId, uint Category = 0, uint Flags = 0);

public sealed record DialogueTopicDraft(string? EditorId, string? Name, float Priority, FormKey? Branch = null,
    FormKey? Keyword = null, byte TopicFlags = 0, byte Category = 0, ushort Subtype = 0, string? SubtypeName = null);

public sealed record DialogueResponseDraft(string? Text, string? ScriptNotes = null, FormKey? Emotion = null,
    FormKey? Sound = null, byte Number = 1, ushort InterruptPercent = 0, int CameraTargetAlias = -1,
    int CameraLocationAlias = -1);

public sealed record DialogueInfoDraft(ushort Flags, ushort ResetTimeEncoded, IReadOnlyList<DialogueResponseDraft> Responses,
    IReadOnlyList<DialogueCondition>? Conditions = null);

public sealed record SceneDraft(string? EditorId, uint Flags = 0, uint Index = 0);

public sealed record FactionRankDraft(DialogueText? MaleTitle, DialogueText? FemaleTitle, byte[]? Insignia = null);

public readonly record struct RecordComponent(FormKey Component, uint Count);

public sealed unsafe partial class PluginFile
{
    public RecordCopyStatus CopyRecordOverride(PluginFile source, FormKey record, bool overwrite = false)
    {
        ArgumentNullException.ThrowIfNull(source);
        using var use = Use();
        using var sourceUse = source.Use();
        using var strings = new NativeStrings();
        RecordCopyStatus status;
        Call.Check(RadicleNative.PluginCopyRecordOverride(use.Pointer, sourceUse.Pointer, strings.Key(record), overwrite ? 1 : 0, &status));
        return status;
    }

    public FormKey CopyRecordAsNew(PluginFile source, FormKey record, string? editorId = null, string? removePrefix = null,
        string? removeSuffix = null, string? prefix = null, string? suffix = null)
    {
        ArgumentNullException.ThrowIfNull(source);
        using var use = Use();
        using var sourceUse = source.Use();
        using var strings = new NativeStrings();
        Native.FormKey key;
        Call.Check(RadicleNative.PluginCopyRecordAsNew(use.Pointer, sourceUse.Pointer, strings.Key(record), editorId,
            removePrefix, removeSuffix, prefix, suffix, &key));
        return FormKey.FromNative(key)!.Value;
    }

    public bool SetGroupMember(FormKey record, string groupPath, int index, string member, bool present)
    {
        var at = Index(index);
        return Edit(record, (plugin, s, key) =>
            RadicleNative.PluginSubrecordGroupMemberSet(plugin, key, groupPath, at, member, present ? 1 : 0));
    }

    public bool InsertScenePhase(FormKey scene, int index, string name)
    {
        var at = Index(index);
        return Edit(scene, (plugin, s, key) => RadicleNative.PluginInsertScenePhase(plugin, key, at, name));
    }

    public bool RemoveScenePhase(FormKey scene, int index)
    {
        var at = Index(index);
        return Edit(scene, (plugin, s, key) => RadicleNative.PluginRemoveScenePhase(plugin, key, at));
    }

    public bool SetScenePhaseName(FormKey scene, int index, string name)
    {
        var at = Index(index);
        return Edit(scene, (plugin, s, key) => RadicleNative.PluginSetScenePhaseName(plugin, key, at, name));
    }

    public bool InsertSceneActor(FormKey scene, int index)
    {
        var at = Index(index);
        return Edit(scene, (plugin, s, key) => RadicleNative.PluginInsertSceneActor(plugin, key, at));
    }

    public bool RemoveSceneActor(FormKey scene, int index)
    {
        var at = Index(index);
        return Edit(scene, (plugin, s, key) => RadicleNative.PluginRemoveSceneActor(plugin, key, at));
    }

    public bool InsertSceneAction(FormKey scene, int index, ushort type)
    {
        var at = Index(index);
        return Edit(scene, (plugin, s, key) => RadicleNative.PluginInsertSceneAction(plugin, key, at, type));
    }

    public bool RemoveSceneAction(FormKey scene, int index)
    {
        var at = Index(index);
        return Edit(scene, (plugin, s, key) => RadicleNative.PluginRemoveSceneAction(plugin, key, at));
    }

    public bool SetSceneActionName(FormKey scene, int index, string name)
    {
        var at = Index(index);
        return Edit(scene, (plugin, s, key) => RadicleNative.PluginSetSceneActionName(plugin, key, at, name));
    }

    public FormKey CreateScene(FormKey quest, SceneDraft draft)
    {
        ArgumentNullException.ThrowIfNull(draft);
        using var use = Use();
        using var strings = new NativeStrings();
        var spec = new Native.SceneSpec { EditorId = strings.Add(draft.EditorId), Flags = draft.Flags, Index = draft.Index };
        Native.FormKey key;
        Call.Check(RadicleNative.PluginCreateScene(use.Pointer, strings.Key(quest), &spec, &key));
        return FormKey.FromNative(key)!.Value;
    }

    public FormKey CreateDialogueBranch(FormKey quest, DialogueBranchDraft draft)
    {
        ArgumentNullException.ThrowIfNull(draft);
        using var use = Use();
        using var strings = new NativeStrings();
        var spec = new Native.DialogueBranchSpec { EditorId = strings.Add(draft.EditorId), Category = draft.Category, Flags = draft.Flags };
        Native.FormKey key;
        Call.Check(RadicleNative.PluginCreateDialogueBranch(use.Pointer, strings.Key(quest), &spec, &key));
        return FormKey.FromNative(key)!.Value;
    }

    public FormKey CreateDialogueTopic(FormKey quest, DialogueTopicDraft draft)
    {
        ArgumentNullException.ThrowIfNull(draft);
        using var use = Use();
        using var strings = new NativeStrings();
        var spec = new Native.DialogueTopicSpec
        {
            EditorId = strings.Add(draft.EditorId), Name = strings.Add(draft.Name), Priority = draft.Priority,
            HasBranch = draft.Branch is null ? 0 : 1, Branch = strings.Key(draft.Branch),
            HasKeyword = draft.Keyword is null ? 0 : 1, Keyword = strings.Key(draft.Keyword),
            TopicFlags = draft.TopicFlags, Category = draft.Category, Subtype = draft.Subtype,
            SubtypeName = strings.Add(draft.SubtypeName),
        };
        Native.FormKey key;
        Call.Check(RadicleNative.PluginCreateDialogueTopic(use.Pointer, strings.Key(quest), &spec, &key));
        return FormKey.FromNative(key)!.Value;
    }

    public bool SetDialogueBranchStart(FormKey branch, FormKey topic)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        return Call.Found(RadicleNative.PluginSetDialogueBranchStart(use.Pointer, strings.Key(branch), strings.Key(topic)));
    }

    public FormKey CreateDialogueInfo(FormKey topic, DialogueInfoDraft draft)
    {
        ArgumentNullException.ThrowIfNull(draft);
        using var use = Use();
        using var strings = new NativeStrings();
        var spec = InfoSpec(draft, strings);
        Native.FormKey key;
        Call.Check(RadicleNative.PluginCreateDialogueInfo(use.Pointer, strings.Key(topic), &spec, &key));
        return FormKey.FromNative(key)!.Value;
    }

    public bool UpdateDialogueInfo(FormKey info, DialogueInfoDraft draft)
    {
        ArgumentNullException.ThrowIfNull(draft);
        using var use = Use();
        using var strings = new NativeStrings();
        var spec = InfoSpec(draft, strings);
        return Call.Found(RadicleNative.PluginUpdateDialogueInfo(use.Pointer, strings.Key(info), &spec));
    }

    public bool RemoveDialogueInfo(FormKey info) =>
        Edit(info, (plugin, s, key) => RadicleNative.PluginRemoveDialogueInfo(plugin, key));

    public FormKey CreateFaction(string? editorId, string? fullName, uint flags = 0)
    {
        using var use = Use();
        Native.FormKey key;
        Call.Check(RadicleNative.PluginCreateFaction(use.Pointer, editorId, fullName, flags, &key));
        return FormKey.FromNative(key)!.Value;
    }

    public bool AddFactionRelation(FormKey faction, FormKey other, int modifier, uint reaction)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        return Call.Found(RadicleNative.PluginAddFactionRelation(use.Pointer, strings.Key(faction), strings.Key(other), modifier, reaction));
    }

    public bool SetFactionRelation(FormKey faction, int index, FormKey other, int modifier, uint reaction)
    {
        var at = Index(index);
        using var use = Use();
        using var strings = new NativeStrings();
        return Call.Found(RadicleNative.PluginSetFactionRelation(use.Pointer, strings.Key(faction), at, strings.Key(other), modifier, reaction));
    }

    public bool RemoveFactionRelation(FormKey faction, int index)
    {
        var at = Index(index);
        return Edit(faction, (plugin, s, key) => RadicleNative.PluginRemoveFactionRelation(plugin, key, at));
    }

    public bool AddFactionRank(FormKey faction, uint number, FactionRankDraft titles)
    {
        ArgumentNullException.ThrowIfNull(titles);
        return Edit(faction, (plugin, s, key) =>
        {
            var input = RankInput(titles, s);
            return RadicleNative.PluginAddFactionRank(plugin, key, number, &input);
        });
    }

    public bool SetFactionRank(FormKey faction, int index, uint number, FactionRankDraft titles)
    {
        ArgumentNullException.ThrowIfNull(titles);
        var at = Index(index);
        return Edit(faction, (plugin, s, key) =>
        {
            var input = RankInput(titles, s);
            return RadicleNative.PluginSetFactionRank(plugin, key, at, number, &input);
        });
    }

    public bool RemoveFactionRank(FormKey faction, int index)
    {
        var at = Index(index);
        return Edit(faction, (plugin, s, key) => RadicleNative.PluginRemoveFactionRank(plugin, key, at));
    }

    public bool SetComponents(FormKey record, IReadOnlyList<RecordComponent> components)
    {
        ArgumentNullException.ThrowIfNull(components);
        return Edit(record, (plugin, s, key) =>
        {
            var native = components.Select(c => new ComponentEntry { Component = s.Key(c.Component), Count = c.Count }).ToArray();
            fixed (ComponentEntry* p = native)
                return RadicleNative.PluginSetRecordComponents(plugin, key, p, (nuint)native.Length);
        });
    }

    private static FactionRankInput RankInput(FactionRankDraft draft, NativeStrings s)
    {
        var insignia = draft.Insignia ?? Array.Empty<byte>();
        return new FactionRankInput
        {
            MaleTitleKind = draft.MaleTitle?.Kind ?? NameKind.Absent, MaleTitleText = s.Add(draft.MaleTitle?.Text),
            MaleTitleStringId = draft.MaleTitle?.StringId ?? 0,
            FemaleTitleKind = draft.FemaleTitle?.Kind ?? NameKind.Absent, FemaleTitleText = s.Add(draft.FemaleTitle?.Text),
            FemaleTitleStringId = draft.FemaleTitle?.StringId ?? 0,
            Insignia = s.AddArray(insignia), InsigniaSize = (nuint)insignia.Length,
        };
    }

    private static Native.DialogueInfoSpec InfoSpec(DialogueInfoDraft draft, NativeStrings s)
    {
        var responses = draft.Responses.Select(r => new Native.DialogueResponseSpec
        {
            Text = s.Add(r.Text), ScriptNotes = s.Add(r.ScriptNotes),
            HasEmotion = r.Emotion is null ? 0 : 1, Emotion = s.Key(r.Emotion),
            HasSound = r.Sound is null ? 0 : 1, Sound = s.Key(r.Sound),
            Number = r.Number, InterruptPercent = r.InterruptPercent,
            CameraTargetAlias = r.CameraTargetAlias, CameraLocationAlias = r.CameraLocationAlias,
        }).ToArray();
        var conditions = (draft.Conditions ?? Array.Empty<DialogueCondition>()).Select(c => new Native.DialogueCondition
        {
            FlagsByte = c.FlagsByte, ComparisonRaw = c.ComparisonRaw, Function = c.Function, Unknown2 = c.Unknown2,
            Param1 = c.Param1, Param2 = c.Param2, RunOn = c.RunOn, Reference = c.Reference, Unknown3 = c.Unknown3,
            Parameter1String = s.Add(c.Parameter1String), Parameter2String = s.Add(c.Parameter2String),
        }).ToArray();
        return new Native.DialogueInfoSpec
        {
            Flags = draft.Flags, ResetTimeEncoded = draft.ResetTimeEncoded,
            Responses = s.AddArray(responses), ResponseCount = (nuint)responses.Length,
            Conditions = s.AddArray(conditions), ConditionCount = (nuint)conditions.Length,
        };
    }
}
