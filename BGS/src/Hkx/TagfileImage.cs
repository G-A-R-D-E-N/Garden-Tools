using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace OpenCommonwealth.Services.Hkx;

public sealed record TagTemplate(string Name, uint Value)
{
    public bool NamesAType => Name.StartsWith("t", StringComparison.Ordinal);
    public override string ToString() => $"{Name}={Value}";
}

public sealed record TagMember(string Name, uint Flags, uint Offset, uint TypeIndex)
{
    public override string ToString() => $"+{Offset} {Name}";
}

public sealed record TagLayout(
    uint TypeIndex,
    uint ParentIndex,
    uint Flags,
    uint Size,
    uint Alignment,
    IReadOnlyList<TagMember> Members)
{
    public override string ToString() =>
        $"type {TypeIndex} size {Size} align {Alignment}, {Members.Count} member(s)";
}

public sealed record TagType(string Name, IReadOnlyList<TagTemplate> Arguments)
{
    public override string ToString() =>
        Arguments.Count == 0 ? Name : $"{Name}<{string.Join(", ", Arguments)}>";
}

public sealed record TagfileImage
{
    public const string RootTag = "TAG0";

    private const uint LengthMask = 0x3FFFFFFF;
    private const int HeaderSize = 8;
    private const int MaxDepth = 8;

    private const uint HasFormat = 0x01;
    private const uint HasSubType = 0x02;
    private const uint HasVersion = 0x04;
    private const uint HasSize = 0x08;
    private const uint HasAbstract = 0x10;
    private const uint HasMembers = 0x20;
    private const uint HasInterface = 0x40;

    public sealed class Section
    {
        public string Name = "";
        public int At;
        public int Length;
        public bool Holds;
        public readonly List<Section> Sections = new();

        public int BodyAt => At + HeaderSize;
        public int BodyLength => Length - HeaderSize;

        public Section? Find(string name)
        {
            if (string.Equals(Name, name, StringComparison.Ordinal)) return this;
            foreach (var section in Sections)
                if (section.Find(name) is { } found) return found;
            return null;
        }

        public override string ToString() =>
            $"{Name} {Length} bytes" + (Holds ? $", {Sections.Count} section(s)" : "");
    }

    public byte[] Data { get; init; } = Array.Empty<byte>();
    public Section Root { get; init; } = new();
    public string SdkVersion { get; init; } = "";
    public IReadOnlyList<string> TypeNames { get; init; } = Array.Empty<string>();
    public IReadOnlyList<string> FieldNames { get; init; } = Array.Empty<string>();
    public IReadOnlyList<TagType> Types { get; init; } = Array.Empty<TagType>();
    public IReadOnlyList<TagLayout> Layouts { get; init; } = Array.Empty<TagLayout>();

    public static bool Looks(ReadOnlySpan<byte> bytes) =>
        bytes.Length >= HeaderSize && Encoding.ASCII.GetString(bytes.Slice(4, 4)) == RootTag;

    public static TagfileImage Read(string path) =>
        Read(File.ReadAllBytes(path), Path.GetFileName(path));

    public static TagfileImage Read(byte[] bytes, string name = "this file")
    {
        ArgumentNullException.ThrowIfNull(bytes);

        if (!Looks(bytes))
            throw new InvalidDataException(
                $"{name} does not start with a {RootTag} section, so it is not a tagfile");

        var root = Parse(bytes, 0, bytes.Length, 0, name);
        var image = new TagfileImage
        {
            Data = bytes,
            Root = root,
            SdkVersion = Text(bytes, root.Find("SDKV")),
            TypeNames = Strings(bytes, root.Find("TSTR")),
            FieldNames = Strings(bytes, root.Find("FSTR")),
        };

        image = image with { Types = ReadTypes(bytes, root.Find("TNAM"), image.TypeNames, name) };
        return image with { Layouts = ReadLayouts(bytes, root.Find("TBOD"), image.FieldNames, name) };
    }

    public static uint Packed(byte[] bytes, ref int at)
    {
        byte lead = bytes[at];

        if ((lead & 0x80) == 0)
        {
            at += 1;
            return lead;
        }

        if ((lead & 0xC0) == 0x80)
        {
            uint value = (uint)(((lead & 0x3F) << 8) | bytes[at + 1]);
            at += 2;
            return value;
        }

        if ((lead & 0xE0) == 0xC0)
        {
            uint value = (uint)(((lead & 0x1F) << 16) | (bytes[at + 1] << 8) | bytes[at + 2]);
            at += 3;
            return value;
        }

        if ((lead & 0xF8) == 0xE0)
        {
            uint value = (uint)(((lead & 0x07) << 24) | (bytes[at + 1] << 16) |
                                (bytes[at + 2] << 8) | bytes[at + 3]);
            at += 4;
            return value;
        }

        if ((lead & 0xF8) == 0xE8)
        {
            uint value = BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(at + 1, 4));
            at += 5;
            return value;
        }

        throw new InvalidDataException(
            $"a packed integer at {at} leads with 0x{lead:x2}, which is not a form this reads");
    }

    private static IReadOnlyList<TagType> ReadTypes(
        byte[] bytes,
        Section? tnam,
        IReadOnlyList<string> names,
        string file)
    {
        if (tnam == null || tnam.BodyLength <= 0) return Array.Empty<TagType>();

        int at = tnam.BodyAt;
        int end = tnam.BodyAt + tnam.BodyLength;
        uint count = Packed(bytes, ref at);
        var types = new List<TagType>(Math.Max(0, (int)count - 1));

        for (uint i = 1; i < count; i++)
        {
            if (at >= end)
                throw new InvalidDataException(
                    $"{file}: TNAM says it holds {count} types and ran out of section at {i}");

            uint name = Packed(bytes, ref at);
            uint parameters = Packed(bytes, ref at);
            var arguments = new List<TagTemplate>((int)parameters);

            for (uint p = 0; p < parameters; p++)
            {
                uint argument = Packed(bytes, ref at);
                uint value = Packed(bytes, ref at);
                arguments.Add(new TagTemplate(Named(names, argument), value));
            }

            types.Add(new TagType(Named(names, name), arguments));
        }

        return types;
    }

    private static IReadOnlyList<TagLayout> ReadLayouts(
        byte[] bytes,
        Section? tbod,
        IReadOnlyList<string> fields,
        string file)
    {
        if (tbod == null || tbod.BodyLength <= 0) return Array.Empty<TagLayout>();

        int at = tbod.BodyAt;
        int end = tbod.BodyAt + tbod.BodyLength;
        var layouts = new List<TagLayout>();

        while (at < end)
        {
            if (Padding(bytes, at, end)) break;

            uint type = Packed(bytes, ref at);
            if (type == 0) break;

            uint parent = Packed(bytes, ref at);
            uint flags = Packed(bytes, ref at);

            if ((flags & HasFormat) != 0) Packed(bytes, ref at);
            if ((flags & HasSubType) != 0) Packed(bytes, ref at);
            if ((flags & HasVersion) != 0) Packed(bytes, ref at);

            uint size = 0;
            uint alignment = 0;
            if ((flags & HasSize) != 0)
            {
                size = Packed(bytes, ref at);
                alignment = Packed(bytes, ref at);
            }

            if ((flags & HasAbstract) != 0) Packed(bytes, ref at);

            var members = new List<TagMember>();
            if ((flags & HasMembers) != 0)
            {
                uint count = Packed(bytes, ref at);
                for (uint m = 0; m < count; m++)
                {
                    if (at >= end)
                        throw new InvalidDataException(
                            $"{file}: a type layout says it has {count} members and the section ran out " +
                            $"after {m}, so the record shape does not fit this file and reading on would " +
                            "be reading noise");

                    uint name = Packed(bytes, ref at);
                    uint memberFlags = Packed(bytes, ref at);
                    uint offset = Packed(bytes, ref at);
                    uint memberType = Packed(bytes, ref at);
                    members.Add(new TagMember(Named(fields, name), memberFlags, offset, memberType));
                }
            }

            if ((flags & HasInterface) != 0) Packed(bytes, ref at);

            if (at > end)
                throw new InvalidDataException(
                    $"{file}: a type layout runs past the end of TBOD, so the record shape does not " +
                    "fit this file and reading on would be reading noise");

            layouts.Add(new TagLayout(type, parent, flags, size, alignment, members));
        }

        return layouts;
    }

    private static bool Padding(byte[] bytes, int at, int end)
    {
        if (end - at >= 8) return false;
        for (int i = at; i < end; i++)
            if (bytes[i] != 0x00 && bytes[i] != 0xFF) return false;
        return true;
    }

    private static string Named(IReadOnlyList<string> names, uint index) =>
        index < names.Count ? names[(int)index] : $"string {index}";

    private static Section Parse(byte[] bytes, int at, int end, int depth, string name)
    {
        uint header = BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(at, 4));
        int length = (int)(header & LengthMask);
        var section = new Section
        {
            Name = Encoding.ASCII.GetString(bytes, at + 4, 4),
            At = at,
            Length = length,
            Holds = (header & ~LengthMask) == 0,
        };

        if (length < HeaderSize || at + length > end)
            throw new InvalidDataException(
                $"{name}: section '{section.Name}' at {at} claims {length} bytes, which does not fit " +
                $"the {end - at} left in the section holding it");

        if (!section.Holds || depth >= MaxDepth) return section;

        int child = at + HeaderSize;
        while (child + HeaderSize <= at + length)
        {
            var inner = Parse(bytes, child, at + length, depth + 1, name);
            section.Sections.Add(inner);
            child += inner.Length;
        }

        return section;
    }

    private static string Text(byte[] bytes, Section? section)
    {
        if (section == null || section.BodyLength <= 0) return "";
        return Encoding.ASCII.GetString(bytes, section.BodyAt, section.BodyLength).TrimEnd('\0').Trim();
    }

    private static IReadOnlyList<string> Strings(byte[] bytes, Section? section)
    {
        if (section == null || section.BodyLength <= 0) return Array.Empty<string>();

        var found = new List<string>();
        int at = section.BodyAt;
        int end = section.BodyAt + section.BodyLength;

        while (at < end)
        {
            int stop = Array.IndexOf(bytes, (byte)0, at, end - at);
            if (stop < 0) stop = end;
            if (stop > at) found.Add(Encoding.ASCII.GetString(bytes, at, stop - at));
            at = stop + 1;
        }

        return found;
    }

    public IEnumerable<Section> Walk()
    {
        var pending = new Stack<Section>();
        pending.Push(Root);

        while (pending.Count > 0)
        {
            var section = pending.Pop();
            yield return section;
            for (int i = section.Sections.Count - 1; i >= 0; i--)
                pending.Push(section.Sections[i]);
        }
    }

    public override string ToString() =>
        $"tagfile at SDK {(SdkVersion.Length > 0 ? SdkVersion : "unknown")}, " +
        $"{TypeNames.Count} type name(s), {FieldNames.Count} field name(s)";
}
