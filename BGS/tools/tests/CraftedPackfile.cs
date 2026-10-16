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

    public NativeAppend.Added Object(string className) => NativeAppend.Object(_image, className);

    public void SetArray(NativeAppend.Added obj, string className, string member, int count, int? target = null)
    {
        var data = _image.Section("__data__")!;
        int field = obj.Offset + HavokClasses.Shipped.Field(className, member)!.Offset;
        data.SetLocal(field, target ?? obj.Offset);
        BitConverter.GetBytes(count).CopyTo(data.Data, field + _image.Layout.PointerSize);
    }

    public int AppendBlock(int size)
    {
        var data = _image.Section("__data__")!;
        data.AlignData(16);
        return data.AppendData(new byte[size]);
    }

    public void SetPointer(int at, int target) => _image.Section("__data__")!.SetLocal(at, target);

    public void SetWord(int at, int value) => BitConverter.GetBytes(value).CopyTo(_image.Section("__data__")!.Data, at);

    public void SetScalar(NativeAppend.Added obj, string className, string member, int value) =>
        BitConverter.GetBytes(value).CopyTo(_image.Section("__data__")!.Data,
            obj.Offset + HavokClasses.Shipped.Field(className, member)!.Offset);

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
