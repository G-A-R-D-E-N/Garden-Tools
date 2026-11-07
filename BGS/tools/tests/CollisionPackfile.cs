using System;
using System.Collections.Generic;
using System.Text;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.Tests;

// Constraint: every size and member offset comes from HavokClassTypes + LayoutWalker, the same table the decoder reads, with no hardcoded struct offset and the table's fit unverified until the corpus run.
internal sealed class CollisionPackfile
{
    private readonly PackfileImage _image = new() { FileVersion = 11 };
    private readonly PackfileSection _data = new();
    private readonly PackfileSection _names = new();
    private readonly Dictionary<string, int> _named = new(StringComparer.Ordinal);

    public CollisionPackfile()
    {
        _image.LayoutRules = new byte[] { 8, 1, 0, 1 };
        _image.Sections.Add(_data);
        _image.Sections.Add(_names);
        _data.TagBytes = Tag("__data__");
        _names.TagBytes = Tag("__classnames__");
        _image.ContentsSectionIndex = 0;
        _image.ContentsClassNameSectionIndex = 1;
    }

    public int AddObject(string className)
    {
        var layout = LayoutWalker.Active(HavokClassTypes.Shipped, className, 8);
        if (layout == null || layout.Size <= 0)
            throw new InvalidOperationException($"{className} has no layout at eight bytes");

        int offset = _data.AppendObject(new byte[layout.Size]);
        _data.AddVirtual(offset, _image.Sections.IndexOf(_names), NameAt(className));
        return offset;
    }

    public int AddPayload(byte[] bytes) => _data.AppendAligned(bytes, 16);

    public void Point(int slotAt, int payloadAt) => _data.SetLocal(slotAt, payloadAt);

    public int Array(int headerAt, byte[] payload, int count, int capacity = -1)
    {
        int at = AddPayload(payload);
        _data.SetLocal(headerAt, at);
        I32(headerAt + 8, count);
        I32(headerAt + 12, capacity < 0 ? count : capacity);
        return at;
    }

    public void U8(int at, byte value) => _data.Data[at] = value;

    public void U16(int at, ushort value) => BitConverter.GetBytes(value).CopyTo(_data.Data, at);

    public void U32(int at, uint value) => BitConverter.GetBytes(value).CopyTo(_data.Data, at);

    public void I32(int at, int value) => BitConverter.GetBytes(value).CopyTo(_data.Data, at);

    public void F32(int at, float value) => BitConverter.GetBytes(value).CopyTo(_data.Data, at);

    public void Half(int at, float value) =>
        BitConverter.GetBytes(BitConverter.HalfToUInt16Bits((Half)value)).CopyTo(_data.Data, at);

    public void RelArray(int headerAt, byte[] payload, int count)
    {
        int at = AddPayload(payload);
        if (at - headerAt > 0xFFFF)
            throw new InvalidOperationException("relative array payload is out of range");
        U32(headerAt, (uint)((at - headerAt) << 16 | (count + 1)));
    }

    public static int Member(string className, string member)
    {
        var layout = LayoutWalker.Active(HavokClassTypes.Shipped, className, 8);
        return layout?.OffsetOf(member)
               ?? throw new InvalidOperationException($"{className} has no member {member}");
    }

    public static int Size(string className) =>
        LayoutWalker.Active(HavokClassTypes.Shipped, className, 8)?.Size
        ?? throw new InvalidOperationException($"{className} has no layout at eight bytes");

    public static byte[] Floats(params float[] values)
    {
        var bytes = new byte[values.Length * 4];
        for (int i = 0; i < values.Length; i++) BitConverter.GetBytes(values[i]).CopyTo(bytes, i * 4);
        return bytes;
    }

    public byte[] Finish() => _image.Rebuild();

    private int NameAt(string className)
    {
        if (_named.TryGetValue(className, out int known)) return known;

        var layout = HavokClassTypes.Shipped[className]
                     ?? throw new InvalidOperationException($"{className} is not in the class table");
        int at = _names.Data.Length;

        var bytes = new List<byte>(BitConverter.GetBytes(layout.Signature));
        bytes.Add(0x09);
        bytes.AddRange(Encoding.ASCII.GetBytes(className));
        bytes.Add(0x00);

        _names.Data = Append(_names.Data, bytes.ToArray());
        _named[className] = at + 5;
        return at + 5;
    }

    private static byte[] Append(byte[] first, byte[] second)
    {
        var joined = new byte[first.Length + second.Length];
        first.CopyTo(joined, 0);
        second.CopyTo(joined, first.Length);
        return joined;
    }

    private static byte[] Tag(string tag)
    {
        var bytes = new byte[20];
        Encoding.ASCII.GetBytes(tag).CopyTo(bytes, 0);
        return bytes;
    }
}
