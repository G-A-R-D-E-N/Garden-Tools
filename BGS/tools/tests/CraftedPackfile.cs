using System;
using System.Text;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.Tests;

internal sealed class CraftedPackfile
{
    private readonly PackfileImage _image = new() { Predicates = new byte[16] };

    public CraftedPackfile()
    {
        foreach (string name in new[] { "__classnames__", "__types__", "__data__" })
        {
            var tag = new byte[20];
            Encoding.ASCII.GetBytes(name).CopyTo(tag, 0);
            _image.Sections.Add(new PackfileSection { TagBytes = tag });
        }
    }

    public PackfileImage Image => _image;

    public PackfileSection Data => _image.Section("__data__")!;

    public NativeAppend.Added Object(string className) => NativeAppend.Object(_image, className);

    public void Attach(NativeAppend.Added from, string member, NativeAppend.Added to) =>
        NativeAppend.Attach(_image, from.Id, member, to.Id);

    public int Offset(NativeAppend.Added obj, string className, string member) =>
        obj.Offset + HavokClasses.Shipped.Field(className, member)!.Offset;

    public void SetArray(NativeAppend.Added obj, string className, string member, int count, int? target = null)
    {
        int field = Offset(obj, className, member);
        Data.SetLocal(field, target ?? obj.Offset);
        SetWord(field + _image.Layout.PointerSize, count);
    }

    public int AppendBlock(int size)
    {
        Data.AlignData(16);
        return Data.AppendData(new byte[size]);
    }

    public int AppendData(byte[] bytes) => Data.AppendData(bytes);

    public void SetPointer(int at, int target) => Data.SetLocal(at, target);

    public void SetWord(int at, int value) => BitConverter.GetBytes(value).CopyTo(Data.Data, at);

    public void SetFloat(int at, float value) => BitConverter.GetBytes(value).CopyTo(Data.Data, at);

    public void SetScalar(NativeAppend.Added obj, string className, string member, int value) =>
        SetWord(Offset(obj, className, member), value);

    public void SetReal(NativeAppend.Added obj, string className, string member, float value) =>
        SetFloat(Offset(obj, className, member), value);

    public byte[] Build(out int dataStart)
    {
        FixupOrder.Reorder(_image);
        byte[] bytes = _image.Rebuild();
        dataStart = 0;
        for (int i = 0; i < 3; i++)
        {
            int header = 0x50 + i * 0x40;
            if (Encoding.ASCII.GetString(bytes, header, 16).TrimEnd('\0') == "__data__")
                dataStart = BitConverter.ToInt32(bytes, header + 0x14);
        }
        return bytes;
    }

    public static byte[] Animation(string animationClass,
                                   Action<CraftedPackfile, NativeAppend.Added>? fill = null)
    {
        var crafted = new CraftedPackfile();
        var animation = crafted.Object(animationClass);
        fill?.Invoke(crafted, animation);
        return crafted.Build(out _);
    }
}
