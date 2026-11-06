using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record VmadShape(
    string Name,
    string Type,
    bool HasAlias = false,
    int? ElementCount = null,
    string? Text = null,
    double? Real = null,
    long? Integer = null,
    bool? Boolean = null,
    string? Object = null,
    string? ElementType = null,
    IReadOnlyList<VmadShape>? Elements = null,
    IReadOnlyList<VmadShape>? Members = null);

public readonly record struct VmadOwner(PapyrusOwnerKind Kind, string BaseFormKey, string Discriminator = "")
{
    public static VmadOwner ForForm(string form) => new(PapyrusOwnerKind.Form, form);

    public static VmadOwner ForPlacedReference(string form, string reference) =>
        new(PapyrusOwnerKind.PlacedReference, form, reference);

    public static VmadOwner ForDynamicReference(string form, string reference) =>
        new(PapyrusOwnerKind.DynamicReference, form, reference);

    public static VmadOwner ForQuestAlias(string quest, int alias) =>
        new(PapyrusOwnerKind.QuestAlias, quest, alias.ToString(System.Globalization.CultureInfo.InvariantCulture));

    public static VmadOwner ForRuntimeInstance(string form, long instance) =>
        new(PapyrusOwnerKind.RuntimeInstance, form, instance.ToString(System.Globalization.CultureInfo.InvariantCulture));
}

public enum VmadBoundKind
{
    Int = 0,
    Float = 1,
    Bool = 2,
    String = 3,
    Object = 4,
    AliasBound = 5,
    Array = 6,
    Struct = 7,
    Variable = 8,
    Unknown = 9,
    Unsupported = 10,
}

public sealed record VmadValue(
    PapyrusValueKind Kind,
    int Integer = 0,
    float Real = 0,
    bool Boolean = false,
    string? Text = null,
    string? ObjectReference = null,
    IReadOnlyList<VmadValue>? Elements = null,
    IReadOnlyDictionary<string, VmadValue>? Members = null);

public sealed record VmadBoundProperty(string OwnerKey, string Name, VmadBoundKind Kind, VmadValue? Value, string? Reason)
{
    public bool IsBound => Value is not null;
}

public sealed record VmadBindReport(
    IReadOnlyList<VmadBoundProperty> Properties,
    int Bound,
    int Unsupported,
    int SilentlyDefaulted,
    IReadOnlyList<string> UnsupportedShapes,
    string Report,
    string Digest)
{
    public bool Clean => SilentlyDefaulted == 0;
}

public static class VmadBinder
{
    public const int MaxDepth = 16;

    public static unsafe VmadBindReport Bind(VmadOwner owner, IEnumerable<VmadShape> shapes)
    {
        var ordered = shapes.OrderBy(shape => shape.Name, Utf8Order.Instance).ToArray();
        var blocks = new List<IntPtr>();
        using var strings = new NativeStrings();
        PapyrusBindReport report = default;
        try
        {
            var native = Build(ordered, strings, blocks, 0);
            Call.Check(RadicleNative.PapyrusBindVmadValues(owner.Kind, owner.BaseFormKey, owner.Discriminator, native,
                (nuint)ordered.Length, &report));
            return Read(report, ordered);
        }
        finally
        {
            RadicleNative.PapyrusBindReportDestroy(&report);
            foreach (var block in blocks) NativeMemory.Free((void*)block);
        }
    }

    private static unsafe PapyrusVmadShape* Build(IReadOnlyList<VmadShape>? shapes, NativeStrings strings,
        List<IntPtr> blocks, int depth)
    {
        if (shapes is null || shapes.Count == 0) return null;
        if (depth > MaxDepth) throw new ArgumentException($"A VMAD shape cannot nest deeper than {MaxDepth} levels.");
        var block = (PapyrusVmadShape*)NativeMemory.AllocZeroed((nuint)shapes.Count, (nuint)sizeof(PapyrusVmadShape));
        blocks.Add((IntPtr)block);
        for (var i = 0; i < shapes.Count; ++i)
        {
            var shape = shapes[i];
            block[i] = new PapyrusVmadShape
            {
                Name = strings.Add(shape.Name),
                Type = strings.Add(shape.Type),
                HasAlias = shape.HasAlias ? 1 : 0,
                HasElementCount = shape.ElementCount.HasValue ? 1 : 0,
                ElementCount = shape.ElementCount ?? 0,
                Text = strings.Add(shape.Text),
                HasReal = shape.Real.HasValue ? 1 : 0,
                Real = shape.Real ?? 0,
                HasInteger = shape.Integer.HasValue ? 1 : 0,
                Integer = shape.Integer ?? 0,
                HasBoolean = shape.Boolean.HasValue ? 1 : 0,
                Boolean = shape.Boolean == true ? 1 : 0,
                Object = strings.Add(shape.Object),
                ElementType = strings.Add(shape.ElementType),
                Elements = (IntPtr)Build(shape.Elements, strings, blocks, depth + 1),
                ElementsLen = (nuint)(shape.Elements?.Count ?? 0),
                Members = (IntPtr)Build(shape.Members, strings, blocks, depth + 1),
                MembersLen = (nuint)(shape.Members?.Count ?? 0),
            };
        }
        return block;
    }

    private static unsafe VmadBindReport Read(PapyrusBindReport report, VmadShape[] ordered)
    {
        var count = checked((int)report.Count);
        var natives = (PapyrusBoundProperty*)report.Properties;
        var properties = new VmadBoundProperty[count];
        for (var i = 0; i < count; ++i)
        {
            var native = natives[i];
            var reason = Utf8.Read(native.Reason);
            var value = native.Value == IntPtr.Zero ? null : ReadValue(native.Value, i < ordered.Length ? ordered[i] : null);
            properties[i] = new VmadBoundProperty(Utf8.Read(native.OwnerKey) ?? string.Empty,
                Utf8.Read(native.Name) ?? string.Empty, (VmadBoundKind)native.Kind, value,
                string.IsNullOrEmpty(reason) ? null : reason);
        }
        var refused = new string[checked((int)report.UnsupportedShapeCount)];
        var texts = (IntPtr*)report.UnsupportedShapes;
        for (var i = 0; i < refused.Length; ++i) refused[i] = Utf8.Read(texts[i]) ?? string.Empty;
        return new VmadBindReport(properties, report.Bound, report.Unsupported, report.SilentlyDefaulted, refused,
            Utf8.Read(report.Report) ?? string.Empty, Utf8.Read(report.Digest) ?? string.Empty);
    }

    private static unsafe VmadValue ReadValue(IntPtr value, VmadShape? guide)
    {
        var kind = RadicleNative.PapyrusValueKindOf(value);
        switch (kind)
        {
            case PapyrusValueKind.Int:
                return new VmadValue(kind, Integer: RadicleNative.PapyrusValueIntOf(value));
            case PapyrusValueKind.Float:
                return new VmadValue(kind, Real: RadicleNative.PapyrusValueFloatOf(value));
            case PapyrusValueKind.Bool:
                return new VmadValue(kind, Boolean: RadicleNative.PapyrusValueBoolOf(value) != 0);
            case PapyrusValueKind.String:
                return new VmadValue(kind, Text: Utf8.Read(RadicleNative.PapyrusValueText(value)) ?? string.Empty);
            case PapyrusValueKind.Object:
                return new VmadValue(kind, ObjectReference: Utf8.Read(RadicleNative.PapyrusValueObjectType(value)) ?? string.Empty);
            case PapyrusValueKind.Array:
            {
                var elements = new VmadValue[checked((int)RadicleNative.PapyrusValueArrayCount(value))];
                for (var i = 0; i < elements.Length; ++i)
                {
                    IntPtr element = IntPtr.Zero;
                    Call.Check(RadicleNative.PapyrusValueArrayGet(value, (nuint)i, &element));
                    try { elements[i] = ReadValue(element, null); }
                    finally { RadicleNative.PapyrusValueDestroy(element); }
                }
                return new VmadValue(kind, Elements: elements);
            }
            case PapyrusValueKind.Struct:
            {
                var members = new Dictionary<string, VmadValue>(StringComparer.OrdinalIgnoreCase);
                foreach (var member in guide?.Members ?? Array.Empty<VmadShape>())
                {
                    IntPtr child = IntPtr.Zero;
                    if (!Call.Found(RadicleNative.PapyrusValueStructGet(value, member.Name, &child))) continue;
                    try { members[member.Name] = ReadValue(child, member); }
                    finally { RadicleNative.PapyrusValueDestroy(child); }
                }
                return new VmadValue(kind, Members: members);
            }
            default:
                return new VmadValue(kind);
        }
    }

    private sealed class Utf8Order : IComparer<string>
    {
        public static readonly Utf8Order Instance = new();

        public int Compare(string? left, string? right) =>
            new ReadOnlySpan<byte>(Encoding.UTF8.GetBytes(left ?? string.Empty))
                .SequenceCompareTo(new ReadOnlySpan<byte>(Encoding.UTF8.GetBytes(right ?? string.Empty)));
    }
}
