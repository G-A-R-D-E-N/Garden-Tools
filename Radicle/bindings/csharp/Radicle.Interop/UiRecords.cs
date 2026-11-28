using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public readonly record struct UiText(NameKind Kind, string? Text, uint StringId);

public sealed record RecordFormLink(string Subrecord, int Ordinal, int Offset, uint RawFormId, FormKey? Key);

public readonly record struct ComponentAmount(FormKey Component, uint Count);

// Constraint: Row holds the raw CTDA ids; Global, Param1, Param2 and Reference are the master-resolved keys, null where the function does not treat that slot as a form.
public sealed record RecipeCondition(RadicleCondition Row, FormKey? Global, FormKey? Param1, FormKey? Param2,
                                     FormKey? Reference);

public sealed record ConstructibleObjectRecord(FormKey Record, FormKey? WorkbenchKeyword, FormKey? CreatedObject,
                                               IReadOnlyList<FormKey> CategoryKeywords,
                                               IReadOnlyList<ComponentAmount> Components, ushort? CreatedCount,
                                               ushort? Priority, UiText Description,
                                               IReadOnlyList<RecipeCondition>? Conditions = null);

public sealed record TerminalBodyEntry(UiText Text, int ConditionCount,
                                       IReadOnlyList<RecipeCondition>? Conditions = null);

public sealed record TerminalMenuEntry(UiText ItemText, UiText ResponseText, UiText DisplayText,
                                       TerminalItemType? Type, byte? RawType, ushort? ItemId, string? Image,
                                       FormKey? Submenu, int ConditionCount,
                                       IReadOnlyList<RecipeCondition>? Conditions = null);

public sealed record TerminalHolotapeEntry(FormKey Holotape, int Count);

public sealed record TerminalRecord(FormKey Record, UiText FullName, UiText HeaderText, UiText WelcomeText,
                                    IReadOnlyList<TerminalBodyEntry> BodyTexts,
                                    IReadOnlyList<TerminalMenuEntry> MenuItems,
                                    IReadOnlyList<TerminalHolotapeEntry> Holotapes);

public sealed record BodyPartNode(float DamageMult, byte Flags, BodyPartType PartType, byte RawPartType,
                                  byte HealthPercent, FormKey? ActorValue, byte ToHitChance,
                                  byte ExplodableExplosionChance, byte NonLethalDismembermentChance,
                                  byte GeometrySegmentIndex);

public sealed record BodyPartRecord(UiText Name, string? PartNode, string? VatsTarget, BodyPartNode? Node);

public sealed record BodyPartDataRecord(FormKey Record, IReadOnlyList<BodyPartRecord> Parts);

// Constraint: Value is null unless the raw value shape is a single float; EntryPoint and Function are raw bytes the engine must apply.
public sealed record PerkEntryPointTab(sbyte RunOn, IReadOnlyList<RecipeCondition> Conditions);

// Constraint: the EPFD shape of an entry point effect, resolved from EPFT and, for EPFT 2, the Function byte; it is not the Function byte itself. An EPFT above 8 is passed through as an undefined enum value with no typed members.
public enum PerkFunctionType : byte
{
    None = 0,
    SingleFloat = 1,
    FloatPair = 2,
    LeveledItem = 3,
    LabeledSpell = 4,
    Spell = 5,
    Text = 6,
    LocalizedText = 7,
    ActorValue = 8,
}

public sealed record PerkActorValue(FormKey? ActorValue, float Amount);

// Constraint: each typed value is non-null only for its FunctionType: Value for SingleFloat, ValuePair for FloatPair, Form for LeveledItem, LabeledSpell and Spell (null for a null link), ActorValue for ActorValue, Text for Text.
public sealed record PerkEntryPointEffect(byte Rank, byte Priority, byte EntryPoint, byte Function, byte? ParameterType,
                                          float? Value, IReadOnlyList<PerkEntryPointTab> Tabs,
                                          PerkFunctionType FunctionType = PerkFunctionType.None,
                                          (float First, float Second)? ValuePair = null, FormKey? Form = null,
                                          PerkActorValue? ActorValue = null, string? Text = null);

public sealed record PerkEntryPointsRecord(FormKey Record, IReadOnlyList<PerkEntryPointEffect> Effects);

// Constraint: the on-disk OMOD property value type; String carries no typed value.
public enum OmodValueType : byte
{
    Int = 0,
    Float = 1,
    Bool = 2,
    String = 3,
    FormInt = 4,
    Enum = 5,
    FormFloat = 6,
}

// Constraint: Value1 and Value2 are the raw little-endian slots; Value1Form is non-null only for value types 4 and 6. Typed members are non-null only for their value type: IntValue for Int (Value1) and FormInt (Value2), FloatValue for Float (Value1) and FormFloat (Value2), BoolValue for Bool, EnumValue for Enum, Form for FormInt and FormFloat (null for a null Value1 link).
public sealed record OmodPropertyEntry(byte ValueType, byte FunctionType, ushort Property, uint Value1, uint Value2,
                                       float Step, FormKey? Value1Form)
{
    public OmodValueType? Kind => ValueType <= (byte)OmodValueType.FormFloat ? (OmodValueType)ValueType : null;

    public int? IntValue => Kind switch
    {
        OmodValueType.Int => unchecked((int)Value1),
        OmodValueType.FormInt => unchecked((int)Value2),
        _ => null,
    };

    public float? FloatValue => Kind switch
    {
        OmodValueType.Float => BitConverter.UInt32BitsToSingle(Value1),
        OmodValueType.FormFloat => BitConverter.UInt32BitsToSingle(Value2),
        _ => null,
    };

    public bool? BoolValue => Kind == OmodValueType.Bool ? Value1 != 0 : null;

    public uint? EnumValue => Kind == OmodValueType.Enum ? Value1 : null;

    public FormKey? Form => Kind is OmodValueType.FormInt or OmodValueType.FormFloat ? Value1Form : null;
}

public sealed record OmodPropertiesRecord(FormKey Record, IReadOnlyList<OmodPropertyEntry> Properties);

public sealed record LockData(bool HasLockData, byte Level, LockKind Kind, FormKey? Key, byte Flags, bool LeveledLock);

public sealed unsafe partial class RadicleEnvironment
{
    private static readonly Encoding Utf8Strict = new UTF8Encoding(false);

    private static FormKey Required(Native.FormKey key) =>
        FormKey.FromNative(key) ?? throw new RadicleException(Status.ParseError, "The record reported an empty key.");

    private UiText UiTextOf(Native.UiText text, int pluginIndex, string recordSignature, string subrecord, StringTables? strings)
    {
        var inline = Utf8.Read(text.Text);
        if (text.Kind != NameKind.StringId || strings is null) return new UiText(text.Kind, inline, text.StringId);
        if (strings.Owner != this) throw new ArgumentException("The strings do not belong to this environment.", nameof(strings));
        var bytes = strings.Lookup(pluginIndex, StringTables.SourceFor(recordSignature, subrecord), text.StringId);
        return new UiText(text.Kind, bytes is null ? null : Utf8Strict.GetString(bytes), text.StringId);
    }

    public IReadOnlyList<RecordFormLink> FormLinks(Record record)
    {
        using var use = Use(record);
        Native.FormLinks* links;
        Call.Check(RadicleNative.RecordFormLinks(use.Pointer, record.Handle, &links));
        try
        {
            var items = (Native.FormLink*)links->Links;
            var result = new RecordFormLink[checked((int)links->LinkCount)];
            for (var i = 0; i < result.Length; ++i)
                result[i] = new RecordFormLink(Encoding.Latin1.GetString(items[i].Subrecord, 4), checked((int)items[i].Ordinal),
                    checked((int)items[i].Offset), items[i].RawFormId, FormKey.FromNative(items[i].Key));
            return result;
        }
        finally { RadicleNative.FormLinksFree(links); }
    }

    public IReadOnlyList<FormKey> FormLinks(Record record, string subrecord) =>
        FormLinks(record).Where(l => l.Subrecord == subrecord && l.Key is not null).Select(l => l.Key!.Value).ToArray();

    public IReadOnlyList<Record> LiveWinnersOf(string signature) =>
        WinnersOf(signature).Where(record => !Info(record).Deleted).ToArray();

    public ConstructibleObjectRecord ConstructibleObject(Record record, StringTables? strings = null)
    {
        var index = Info(record).PluginIndex;
        using var use = Use(record);
        Native.ConstructibleObject* o;
        Call.Check(RadicleNative.ConstructibleObjectDecode(use.Pointer, record.Handle, &o));
        try
        {
            return new ConstructibleObjectRecord(Required(o->Record), FormKey.FromNative(o->WorkbenchKeyword),
                FormKey.FromNative(o->CreatedObject),
                Call.Copy<Native.FormKey>(o->CategoryKeywords, o->CategoryKeywordCount).Select(Required).ToArray(),
                Call.Copy<Native.ComponentEntry>(o->Components, o->ComponentCount)
                    .Select(c => (Key: FormKey.FromNative(c.Component), c.Count))
                    .Where(c => c.Key is not null).Select(c => new ComponentAmount(c.Key!.Value, c.Count)).ToArray(),
                o->HasCreatedCount == 0 ? null : o->CreatedCount, o->HasPriority == 0 ? null : o->Priority,
                UiTextOf(o->Description, index, "COBJ", "DESC", strings), RecipeConditions(o));
        }
        finally { RadicleNative.ConstructibleObjectFree(o); }
    }

    private static RecipeCondition[] RecipeConditions(Native.ConstructibleObject* o) =>
        ConditionRows(o->Conditions, o->ConditionLinks, o->ConditionCount);

    private static RecipeCondition[] ConditionRows(IntPtr conditions, IntPtr conditionLinks, nuint count)
    {
        var rows = Call.Copy<Native.Condition>(conditions, count);
        var links = Call.Copy<Native.ConditionLinks>(conditionLinks, count);
        return rows.Select((c, i) => new RecipeCondition(new RadicleCondition(c.FlagsByte, c.ComparisonRaw, c.Function,
            c.Unknown2, c.Param1, c.Param2, c.RunOn, c.Reference, c.Unknown3, Utf8.Read(c.Parameter1String),
            Utf8.Read(c.Parameter2String)), FormKey.FromNative(links[i].Global), FormKey.FromNative(links[i].Param1),
            FormKey.FromNative(links[i].Param2), FormKey.FromNative(links[i].Reference))).ToArray();
    }

    public TerminalRecord Terminal(Record record, StringTables? strings = null)
    {
        var index = Info(record).PluginIndex;
        using var use = Use(record);
        Native.TerminalInfo* t;
        Call.Check(RadicleNative.TerminalDecode(use.Pointer, record.Handle, &t));
        try
        {
            return new TerminalRecord(Required(t->Record), UiTextOf(t->FullName, index, "TERM", "FULL", strings),
                UiTextOf(t->HeaderText, index, "TERM", "NAM0", strings), UiTextOf(t->WelcomeText, index, "TERM", "WNAM", strings),
                Call.Copy<Native.TerminalBodyText>(t->BodyTexts, t->BodyTextCount).Select(b =>
                    new TerminalBodyEntry(UiTextOf(b.Text, index, "TERM", "BTXT", strings), checked((int)b.ConditionCount),
                        ConditionRows(b.Conditions, b.ConditionLinks, b.ConditionCount))).ToArray(),
                Call.Copy<Native.TerminalMenuItem>(t->MenuItems, t->MenuItemCount).Select(m =>
                    new TerminalMenuEntry(UiTextOf(m.ItemText, index, "TERM", "ITXT", strings),
                        UiTextOf(m.ResponseText, index, "TERM", "RNAM", strings),
                        UiTextOf(m.DisplayText, index, "TERM", "UNAM", strings),
                        m.HasType == 0 ? null : (TerminalItemType)m.Type, m.HasType == 0 ? null : m.Type,
                        m.HasItemId == 0 ? null : m.ItemId, Utf8.Read(m.Image), FormKey.FromNative(m.Submenu),
                        checked((int)m.ConditionCount), ConditionRows(m.Conditions, m.ConditionLinks, m.ConditionCount))).ToArray(),
                Call.Copy<Native.TerminalHolotape>(t->Holotapes, t->HolotapeCount)
                    .Select(h => new TerminalHolotapeEntry(Required(h.Holotape), h.Count)).ToArray());
        }
        finally { RadicleNative.TerminalFree(t); }
    }

    public PerkEntryPointsRecord? PerkEntryPoints(FormKey perk)
    {
        var winner = Winner(perk);
        return winner is null ? null : PerkEntryPoints(winner.Value);
    }

    public PerkEntryPointsRecord PerkEntryPoints(Record record)
    {
        using var use = Use(record);
        Native.PerkEntryPointsInfo* p;
        Call.Check(RadicleNative.PerkEntryPointsDecode(use.Pointer, record.Handle, &p));
        try
        {
            Native.PerkEntryPointsTypedInfo* t;
            Call.Check(RadicleNative.PerkEntryPointsTypedDecode(use.Pointer, record.Handle, &t));
            try
            {
                var typed = Call.Copy<Native.PerkEntryPointEffectTyped>(t->Effects, t->EffectCount);
                return new PerkEntryPointsRecord(Required(p->Record), Call.Copy<Native.PerkEntryPointEffect>(p->Effects, p->EffectCount)
                    .Select((e, i) => new PerkEntryPointEffect(e.Rank, e.Priority, e.EntryPoint, e.Function,
                        e.HasParameterType == 0 ? null : e.ParameterType, e.HasValue == 0 ? null : e.Value,
                        Call.Copy<Native.PerkEntryPointTab>(e.Tabs, e.TabCount)
                            .Select(tab => new PerkEntryPointTab(tab.RunOn, ConditionRows(tab.Conditions, tab.ConditionLinks, tab.ConditionCount)))
                            .ToArray(), (PerkFunctionType)typed[i].FunctionType,
                        typed[i].HasValuePair == 0 ? null : (typed[i].ValuePairFirst, typed[i].ValuePairSecond),
                        FormKey.FromNative(typed[i].Form),
                        typed[i].HasActorValue == 0 ? null : new PerkActorValue(FormKey.FromNative(typed[i].ActorValue), typed[i].ActorValueAmount),
                        Utf8.Read(typed[i].Text))).ToArray());
            }
            finally { RadicleNative.PerkEntryPointsTypedFree(t); }
        }
        finally { RadicleNative.PerkEntryPointsFree(p); }
    }

    public OmodPropertiesRecord? OmodProperties(FormKey omod)
    {
        var winner = Winner(omod);
        return winner is null ? null : OmodProperties(winner.Value);
    }

    public OmodPropertiesRecord OmodProperties(Record record)
    {
        using var use = Use(record);
        Native.OmodPropertiesInfo* p;
        Call.Check(RadicleNative.OmodPropertiesDecode(use.Pointer, record.Handle, &p));
        try
        {
            var rows = Call.Copy<Native.OmodPropertyRow>(p->Properties, p->PropertyCount);
            var properties = new OmodPropertyEntry[rows.Length];
            for (var i = 0; i < rows.Length; ++i)
            {
                var row = rows[i];
                properties[i] = new OmodPropertyEntry(row.ValueType, row.FunctionType, row.Property,
                    QuadToUInt32(row.Value1), QuadToUInt32(row.Value2), row.Step, FormKey.FromNative(row.Value1Form));
            }
            return new OmodPropertiesRecord(Required(p->Record), properties);
        }
        finally { RadicleNative.OmodPropertiesFree(p); }
    }

    private static uint QuadToUInt32(byte* quad) => (uint)(quad[0] | quad[1] << 8 | quad[2] << 16 | quad[3] << 24);

    public BodyPartDataRecord BodyPartData(Record record, StringTables? strings = null)
    {
        var index = Info(record).PluginIndex;
        using var use = Use(record);
        Native.BodyPartDataInfo* b;
        Call.Check(RadicleNative.BodyPartDataDecode(use.Pointer, record.Handle, &b));
        try
        {
            return new BodyPartDataRecord(Required(b->Record), Call.Copy<Native.BodyPartInfo>(b->Parts, b->PartCount).Select(p =>
                new BodyPartRecord(UiTextOf(p.Name, index, "BPTD", "BPTN", strings), Utf8.Read(p.PartNode), Utf8.Read(p.VatsTarget),
                    p.HasData == 0 ? null : new BodyPartNode(p.DamageMult, p.Flags, (BodyPartType)p.PartType, p.PartType,
                        p.HealthPercent, FormKey.FromNative(p.ActorValue), p.ToHitChance, p.ExplodableExplosionChance,
                        p.NonLethalDismembermentChance, p.GeometrySegmentIndex))).ToArray());
        }
        finally { RadicleNative.BodyPartDataFree(b); }
    }

    public LockData Lock(Record record)
    {
        using var use = Use(record);
        Native.LockInfo info;
        Call.Check(RadicleNative.LockInfoRead(use.Pointer, record.Handle, &info));
        return new LockData(info.HasLockData != 0, info.Level, info.Kind, FormKey.FromNative(info.Key), info.Flags,
            info.LeveledLock != 0);
    }
}
