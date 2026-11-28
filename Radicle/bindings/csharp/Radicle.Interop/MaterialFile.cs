using System;
using System.Collections.Generic;
using System.Text;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public enum MaterialFormat
{
    Bgsm = 0,
    Bgem = 1,
}

public enum MaterialFieldType
{
    Bool = 0,
    U8 = 1,
    U32 = 2,
    F32 = 3,
    Color3 = 4,
    String = 5,
}

public readonly record struct MaterialField(string Name, MaterialFieldType Type);

public readonly record struct MaterialColor(float R, float G, float B);

public sealed class MaterialFile : IDisposable
{
    private IntPtr _handle;

    private MaterialFile(IntPtr handle, MaterialFormat format, uint version, IReadOnlyList<MaterialField> fields)
    {
        _handle = handle;
        Format = format;
        Version = version;
        Fields = fields;
    }

    public MaterialFormat Format { get; }

    public uint Version { get; }

    public IReadOnlyList<MaterialField> Fields { get; }

    public static unsafe MaterialFile FromBytes(ReadOnlySpan<byte> bytes)
    {
        IntPtr handle = IntPtr.Zero;
        fixed (byte* pointer = bytes)
        {
            Call.Check(RadicleNative.MaterialFileRead(bytes.IsEmpty ? null : pointer, (nuint)bytes.Length, &handle));
        }
        return Snapshot(handle);
    }

    public unsafe byte[] ToBytes()
    {
        byte* bytes = null;
        nuint size = 0;
        Call.Check(RadicleNative.MaterialFileWrite(Live(), &bytes, &size));
        return Call.TakeBytes(bytes, size);
    }

    public unsafe bool GetBool(string name)
    {
        byte value = 0;
        Call.Check(RadicleNative.MaterialFileGetBool(Live(), name, &value));
        return value != 0;
    }

    public unsafe byte GetU8(string name)
    {
        byte value = 0;
        Call.Check(RadicleNative.MaterialFileGetU8(Live(), name, &value));
        return value;
    }

    public unsafe uint GetU32(string name)
    {
        uint value = 0;
        Call.Check(RadicleNative.MaterialFileGetU32(Live(), name, &value));
        return value;
    }

    public unsafe float GetF32(string name)
    {
        float value = 0;
        Call.Check(RadicleNative.MaterialFileGetF32(Live(), name, &value));
        return value;
    }

    public unsafe MaterialColor GetColor(string name)
    {
        Native.MaterialColor3 raw = default;
        Call.Check(RadicleNative.MaterialFileGetColor3(Live(), name, &raw));
        return new MaterialColor(raw.R, raw.G, raw.B);
    }

    public unsafe string GetText(string name)
    {
        byte* bytes = null;
        nuint size = 0;
        Call.Check(RadicleNative.MaterialFileGetString(Live(), name, &bytes, &size));
        return Encoding.Latin1.GetString(Call.TakeBytes(bytes, size));
    }

    public void SetBool(string name, bool value) =>
        Call.Check(RadicleNative.MaterialFileSetBool(Live(), name, value ? (byte)1 : (byte)0));

    public void SetU8(string name, byte value) => Call.Check(RadicleNative.MaterialFileSetU8(Live(), name, value));

    public void SetU32(string name, uint value) => Call.Check(RadicleNative.MaterialFileSetU32(Live(), name, value));

    public void SetF32(string name, float value) => Call.Check(RadicleNative.MaterialFileSetF32(Live(), name, value));

    public unsafe void SetColor(string name, MaterialColor value)
    {
        var raw = new Native.MaterialColor3 { R = value.R, G = value.G, B = value.B };
        Call.Check(RadicleNative.MaterialFileSetColor3(Live(), name, &raw));
    }

    public unsafe void SetText(string name, string text)
    {
        var bytes = Encoding.Latin1.GetBytes(text);
        fixed (byte* pointer = bytes)
        {
            Call.Check(RadicleNative.MaterialFileSetString(Live(), name, pointer, (nuint)bytes.Length));
        }
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero) return;
        RadicleNative.MaterialFileDestroy(_handle);
        _handle = IntPtr.Zero;
    }

    private IntPtr Live() => _handle == IntPtr.Zero ? throw new ObjectDisposedException(nameof(MaterialFile)) : _handle;

    private static unsafe MaterialFile Snapshot(IntPtr handle)
    {
        try
        {
            Native.MaterialFormat rawFormat = default;
            Call.Check(RadicleNative.MaterialFileFormat(handle, &rawFormat));

            uint version = 0;
            Call.Check(RadicleNative.MaterialFileVersion(handle, &version));

            nuint count = 0;
            Call.Check(RadicleNative.MaterialFileFieldCount(handle, &count));
            var fields = new List<MaterialField>(checked((int)count));
            for (var index = 0; index < checked((int)count); ++index)
            {
                byte* name = null;
                nuint nameSize = 0;
                Call.Check(RadicleNative.MaterialFileFieldName(handle, (nuint)index, &name, &nameSize));
                Native.MaterialFieldType rawType = default;
                Call.Check(RadicleNative.MaterialFileFieldType(handle, (nuint)index, &rawType));
                fields.Add(new MaterialField(Encoding.Latin1.GetString(Call.TakeBytes(name, nameSize)), (MaterialFieldType)rawType));
            }

            return new MaterialFile(handle, (MaterialFormat)rawFormat, version, fields);
        }
        catch
        {
            RadicleNative.MaterialFileDestroy(handle);
            throw;
        }
    }
}
