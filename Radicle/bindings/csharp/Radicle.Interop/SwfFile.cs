using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public enum SwfOwner
{
    Class = 0,
    Script = 1,
    Function = 2,
}

public sealed record SwfMovieInfo(
    byte Format,
    byte Compression,
    byte Version,
    bool ActionScript3,
    bool HasMetadata,
    bool UseNetwork,
    bool UseGpu,
    bool UseDirectBlit,
    bool DeclaredLengthMatches,
    ushort FrameCount,
    float FrameRate,
    uint FileLength,
    int TagCount,
    int ShapeTags,
    int BitmapTags,
    int FontTags,
    int TextTags,
    int SoundTags,
    int ExporterInfoTags,
    int ExternalImageTags,
    int FontTextureTags,
    int SymbolCount,
    int ExportCount,
    int FrameLabelCount,
    int AbcCount);

public sealed record SwfTagRecord(
    int Index,
    ushort Code,
    string Name,
    uint Length,
    long Offset,
    int Parent,
    ushort SpriteId,
    bool InsideSprite,
    bool LongHeader,
    long PayloadSize);

public sealed record SwfSymbolRecord(ushort TagId, string Name);

public sealed record SwfFrameLabelRecord(string Name, int TagIndex, ushort SpriteId, bool InsideSprite);

public sealed record SwfCallSite(
    long Offset,
    string Opcode,
    string Target,
    string Receiver,
    int ArgumentCount,
    string EventName,
    string Handler,
    bool IsExternalInterface,
    bool IsEventRegistration);

public sealed record SwfMethodSummary(string Name, string ReturnType, uint Flags, int InstructionCount, int CallCount,
                                      int MultinameCount);

public sealed record SwfClassSummary(string Name, string SuperName, IReadOnlyList<string> Interfaces,
                                     IReadOnlyList<SwfMethodSummary> Methods);

public sealed record SwfAbcBlock(string Name, uint Flags, int TagIndex, long PayloadSize, bool Parsed,
                                 IReadOnlyList<SwfClassSummary> Classes, IReadOnlyList<SwfMethodSummary> Scripts,
                                 IReadOnlyList<SwfMethodSummary> Functions);

public sealed record SwfMethodBody(string Name, string ReturnType, int ParamCount, byte Flags, bool HasBody,
                                   uint MaxStack, uint LocalCount, uint InitScopeDepth, uint MaxScopeDepth,
                                   uint CodeSize, int ExceptionCount);

internal sealed class SwfHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    public SwfHandle(IntPtr handle) : base(ownsHandle: true) => SetHandle(handle);

    protected override bool ReleaseHandle()
    {
        RadicleNative.SwfDestroy(handle);
        return true;
    }
}

// A parsed movie; reads must happen once at open time so the same bytes give the same records.
public sealed unsafe partial class SwfFile : IDisposable
{
    private readonly SwfHandle _handle;

    static SwfFile() => RadicleLibrary.Register();

    private SwfFile(IntPtr handle)
    {
        _handle = new SwfHandle(handle);
        try
        {
            Info = ReadInfo(_handle);
            Tags = ReadTags(_handle, Info.TagCount);
            Symbols = ReadSymbols(_handle, Info.SymbolCount, exports: false);
            Exports = ReadSymbols(_handle, Info.ExportCount, exports: true);
            FrameLabels = ReadFrameLabels(_handle, Info.FrameLabelCount);
            AbcBlocks = ReadAbcBlocks(_handle, Info.AbcCount);
            Shapes = ReadShapes(_handle, Tags);
            Bitmaps = ReadBitmaps(_handle, Tags);
            Jpegs = ReadJpegs(_handle, Tags);
            JpegTables = ReadJpegTables(_handle, Tags);
            Morphs = ReadMorphs(_handle, Tags);
        }
        catch
        {
            _handle.Dispose();
            throw;
        }
    }

    public SwfMovieInfo Info { get; }

    public IReadOnlyList<SwfTagRecord> Tags { get; }

    public IReadOnlyList<SwfSymbolRecord> Symbols { get; }

    public IReadOnlyList<SwfSymbolRecord> Exports { get; }

    public IReadOnlyList<SwfFrameLabelRecord> FrameLabels { get; }

    public IReadOnlyList<SwfAbcBlock> AbcBlocks { get; }

    public static SwfFile FromBytes(ReadOnlySpan<byte> bytes)
    {
        IntPtr handle;
        fixed (byte* pointer = bytes)
        {
            Call.Check(RadicleNative.SwfOpenBytes(bytes.IsEmpty ? null : pointer, (nuint)bytes.Length, &handle));
        }
        return new SwfFile(handle);
    }

    public static SwfFile FromFile(string path)
    {
        IntPtr handle;
        Call.Check(RadicleNative.SwfOpenPath(path, &handle));
        return new SwfFile(handle);
    }

    public static SwfFile FromArchive(Ba2Archive archive, string path)
    {
        using var use = archive.Use();
        IntPtr handle;
        Call.Check(RadicleNative.SwfOpenBa2(use.Pointer, path, &handle));
        return new SwfFile(handle);
    }

    private HandleUse Use() => new(_handle, nameof(SwfFile));

    public byte[] Payload(int tagIndex)
    {
        using var use = Use();
        byte* bytes = null;
        nuint size = 0;
        Call.Check(RadicleNative.SwfTagPayload(use.Pointer, (nuint)tagIndex, &bytes, &size));
        try
        {
            return Utf8.Raw((IntPtr)bytes, size);
        }
        finally
        {
            if (bytes != null) RadicleNative.FreeBytes(bytes);
        }
    }

    public IReadOnlyList<SwfCallSite> Calls(int abc, SwfOwner owner, int ownerIndex, int methodIndex)
    {
        using var use = Use();
        var method = ReadMethod(use.Pointer, abc, owner, ownerIndex, methodIndex);
        var calls = new List<SwfCallSite>((int)method.CallCount);
        for (var index = 0; index < (int)method.CallCount; ++index)
        {
            SwfCall call;
            Call.Check(RadicleNative.SwfAbcCallAt(use.Pointer, (nuint)abc, (nuint)(int)owner, (nuint)(int)ownerIndex,
                                                 (nuint)methodIndex, (nuint)index, &call));
            calls.Add(new SwfCallSite(
                (long)call.Offset,
                Utf8.Read(call.Opcode) ?? string.Empty,
                Utf8.Read(call.Target) ?? string.Empty,
                Utf8.Read(call.Receiver) ?? string.Empty,
                (int)call.ArgumentCount,
                Utf8.Read(call.EventName) ?? string.Empty,
                Utf8.Read(call.Handler) ?? string.Empty,
                call.IsExternalInterface != 0,
                call.IsEventRegistration != 0));
        }
        return calls;
    }

    public IReadOnlyList<string> Multinames(int abc, SwfOwner owner, int ownerIndex, int methodIndex)
    {
        using var use = Use();
        var method = ReadMethod(use.Pointer, abc, owner, ownerIndex, methodIndex);
        var names = new List<string>((int)method.MultinameCount);
        for (var index = 0; index < (int)method.MultinameCount; ++index)
        {
            IntPtr name;
            Call.Check(RadicleNative.SwfAbcMultinameAt(use.Pointer, (nuint)abc, (nuint)(int)owner, (nuint)(int)ownerIndex,
                                                      (nuint)methodIndex, (nuint)index, &name));
            names.Add(Utf8.Read(name) ?? string.Empty);
        }
        return names;
    }

    public IReadOnlyList<SwfMethodBody> Methods(int abc)
    {
        using var use = Use();
        var count = (int)RadicleNative.SwfAbcMethodCount(use.Pointer, (nuint)abc);
        var methods = new List<SwfMethodBody>(count);
        for (var index = 0; index < count; ++index)
        {
            SwfMethodPool method;
            Call.Check(RadicleNative.SwfAbcMethodAt(use.Pointer, (nuint)abc, (nuint)index, &method));
            methods.Add(new SwfMethodBody(
                Utf8.Read(method.Name) ?? string.Empty,
                Utf8.Read(method.ReturnType) ?? string.Empty,
                (int)method.ParamCount,
                method.Flags,
                method.HasBody != 0,
                method.MaxStack,
                method.LocalCount,
                method.InitScopeDepth,
                method.MaxScopeDepth,
                method.CodeSize,
                (int)method.ExceptionCount));
        }
        return methods;
    }

    public string Disassemble(int abc, int methodIndex)
    {
        using var use = Use();
        byte* text = null;
        nuint size = 0;
        Call.Check(RadicleNative.SwfAbcMethodDisassemble(use.Pointer, (nuint)abc, (nuint)methodIndex, &text, &size));
        try
        {
            return System.Text.Encoding.UTF8.GetString(Utf8.Raw((IntPtr)text, size));
        }
        finally
        {
            if (text != null) RadicleNative.FreeBytes(text);
        }
    }

    public string InspectText(string name)
    {
        using var use = Use();
        byte* text = null;
        nuint size = 0;
        Call.Check(RadicleNative.SwfInspectText(use.Pointer, name, &text, &size));
        return Call.TakeText(text, size);
    }

    public string Avm1Disassemble(int tagIndex)
    {
        using var use = Use();
        byte* text = null;
        nuint size = 0;
        Call.Check(RadicleNative.SwfAvm1Disassemble(use.Pointer, (nuint)tagIndex, &text, &size));
        return Call.TakeText(text, size);
    }

    public byte[] BitmapPng(int flatTagIndex)
    {
        using var use = Use();
        byte* bytes = null;
        nuint size = 0;
        Call.Check(RadicleNative.SwfBitmapPng(use.Pointer, (nuint)flatTagIndex, &bytes, &size));
        try
        {
            return Utf8.Raw((IntPtr)bytes, size);
        }
        finally
        {
            if (bytes != null) RadicleNative.FreeBytes(bytes);
        }
    }

    public string AbcText(int abc)
    {
        using var use = Use();
        byte* text = null;
        nuint size = 0;
        Call.Check(RadicleNative.SwfAbcText(use.Pointer, (nuint)abc, &text, &size));
        try
        {
            return System.Text.Encoding.UTF8.GetString(Utf8.Raw((IntPtr)text, size));
        }
        finally
        {
            if (text != null) RadicleNative.FreeBytes(text);
        }
    }

    public string ShapeSvg(ushort characterId)
    {
        using var use = Use();
        byte* text = null;
        nuint size = 0;
        Call.Check(RadicleNative.SwfShapeSvg(use.Pointer, characterId, &text, &size));
        try
        {
            return System.Text.Encoding.UTF8.GetString(Utf8.Raw((IntPtr)text, size));
        }
        finally
        {
            if (text != null) RadicleNative.FreeBytes(text);
        }
    }

    public string FrameSvg(uint frame)
    {
        using var use = Use();
        byte* text = null;
        nuint size = 0;
        Call.Check(RadicleNative.SwfFrameSvg(use.Pointer, frame, &text, &size));
        try
        {
            return System.Text.Encoding.UTF8.GetString(Utf8.Raw((IntPtr)text, size));
        }
        finally
        {
            if (text != null) RadicleNative.FreeBytes(text);
        }
    }

    public byte[] AssembleAbc(int abc, string text)
    {
        using var use = Use();
        var utf8 = System.Text.Encoding.UTF8.GetBytes(text);
        byte* outBytes = null;
        nuint outSize = 0;
        unsafe
        {
            fixed (byte* input = utf8)
            {
                Call.Check(RadicleNative.SwfAbcAssemble(use.Pointer, (nuint)abc, input, (nuint)utf8.Length,
                                                        &outBytes, &outSize));
            }
        }
        try
        {
            return Utf8.Raw((IntPtr)outBytes, outSize);
        }
        finally
        {
            if (outBytes != null) RadicleNative.FreeBytes(outBytes);
        }
    }

    public void ApplyAbcText(int abc, string text)
    {
        using var use = Use();
        var utf8 = System.Text.Encoding.UTF8.GetBytes(text);
        unsafe
        {
            fixed (byte* input = utf8)
            {
                Call.Check(RadicleNative.SwfAbcApplyText(use.Pointer, (nuint)abc, input, (nuint)utf8.Length));
            }
        }
    }

    public void InsertAbcInstruction(int abc, int body, int offset, string text)
    {
        using var use = Use();
        var utf8 = System.Text.Encoding.UTF8.GetBytes(text);
        unsafe
        {
            fixed (byte* input = utf8)
            {
                Call.Check(RadicleNative.SwfAbcInsertInstruction(use.Pointer, (nuint)abc, (uint)body, (nuint)offset,
                                                                 input, (nuint)utf8.Length));
            }
        }
    }

    public void ReplaceAbcInstruction(int abc, int body, int offset, string text)
    {
        using var use = Use();
        var utf8 = System.Text.Encoding.UTF8.GetBytes(text);
        unsafe
        {
            fixed (byte* input = utf8)
            {
                Call.Check(RadicleNative.SwfAbcReplaceInstruction(use.Pointer, (nuint)abc, (uint)body, (nuint)offset,
                                                                  input, (nuint)utf8.Length));
            }
        }
    }

    public void ReplaceFontGlyph(int index, int glyph, byte fillBits, byte lineBits,
                                 IReadOnlyList<SwfShapeCommandRecord> records)
    {
        using var use = Use();
        var native = new Native.SwfShapeRecord[records.Count];
        for (var item = 0; item < native.Length; ++item)
        {
            var record = records[item];
            native[item] = new Native.SwfShapeRecord
            {
                Kind = ToRecordKind(record.Kind), X = record.X, Y = record.Y, ControlX = record.ControlX,
                ControlY = record.ControlY, Fill0 = record.Fill0, Fill1 = record.Fill1, Line = record.Line,
            };
        }
        SwfPlanResult result;
        unsafe
        {
            fixed (Native.SwfShapeRecord* input = native)
            {
                Call.Check(RadicleNative.SwfFontReplaceGlyph(use.Pointer, (nuint)index, (nuint)glyph, fillBits, lineBits,
                                                             input, (nuint)native.Length, &result));
            }
        }
    }

    private static SwfRecordKind ToRecordKind(SwfShapeCommandKind kind) => kind switch
    {
        SwfShapeCommandKind.Move => SwfRecordKind.Move,
        SwfShapeCommandKind.StyleChange => SwfRecordKind.StyleChange,
        SwfShapeCommandKind.StraightEdge => SwfRecordKind.StraightEdge,
        SwfShapeCommandKind.CurvedEdge => SwfRecordKind.CurvedEdge,
        _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "unknown shape record kind"),
    };

    public byte[] WriteBytes()
    {
        using var use = Use();
        byte* bytes = null;
        nuint size = 0;
        Call.Check(RadicleNative.SwfWriteBytes(use.Pointer, &bytes, &size));
        try
        {
            return Utf8.Raw((IntPtr)bytes, size);
        }
        finally
        {
            if (bytes != null) RadicleNative.FreeBytes(bytes);
        }
    }

    public void Dispose() => _handle.Dispose();

    private static SwfMovieInfo ReadInfo(SwfHandle handle)
    {
        using var use = new HandleUse(handle, nameof(SwfFile));
        Radicle.Interop.Native.SwfInfo native;
        Call.Check(RadicleNative.SwfGetInfo(use.Pointer, &native));
        return new SwfMovieInfo(
            native.Format, native.Compression, native.Version,
            native.ActionScript3 != 0, native.HasMetadata != 0, native.UseNetwork != 0, native.UseGpu != 0,
            native.UseDirectBlit != 0, native.DeclaredLengthMatches != 0,
            native.FrameCount, native.FrameRate, native.FileLength,
            (int)native.TagCount, (int)native.ShapeTags, (int)native.BitmapTags, (int)native.FontTags,
            (int)native.TextTags, (int)native.SoundTags, (int)native.ExporterInfoTags, (int)native.ExternalImageTags,
            (int)native.FontTextureTags, (int)native.SymbolCount, (int)native.ExportCount,
            (int)native.FrameLabelCount, (int)native.AbcCount);
    }

    private static IReadOnlyList<SwfTagRecord> ReadTags(SwfHandle handle, int count)
    {
        using var use = new HandleUse(handle, nameof(SwfFile));
        var tags = new List<SwfTagRecord>(count);
        for (var index = 0; index < count; ++index)
        {
            SwfTag tag;
            Call.Check(RadicleNative.SwfTagAt(use.Pointer, (nuint)index, &tag));
            tags.Add(new SwfTagRecord(index, tag.Code, Utf8.Read(tag.Name) ?? string.Empty, tag.Length, (long)tag.Offset,
                                      tag.Parent == nuint.MaxValue ? -1 : (int)tag.Parent, tag.SpriteId,
                                      tag.InsideSprite != 0, tag.LongHeader != 0, (long)tag.PayloadSize));
        }
        return tags;
    }

    private static IReadOnlyList<SwfSymbolRecord> ReadSymbols(SwfHandle handle, int count, bool exports)
    {
        using var use = new HandleUse(handle, nameof(SwfFile));
        var symbols = new List<SwfSymbolRecord>(count);
        for (var index = 0; index < count; ++index)
        {
            SwfSymbol symbol;
            var status = exports
                ? RadicleNative.SwfExportAt(use.Pointer, (nuint)index, &symbol)
                : RadicleNative.SwfSymbolAt(use.Pointer, (nuint)index, &symbol);
            Call.Check(status);
            symbols.Add(new SwfSymbolRecord(symbol.TagId, Utf8.Read(symbol.Name) ?? string.Empty));
        }
        return symbols;
    }

    private static IReadOnlyList<SwfFrameLabelRecord> ReadFrameLabels(SwfHandle handle, int count)
    {
        using var use = new HandleUse(handle, nameof(SwfFile));
        var labels = new List<SwfFrameLabelRecord>(count);
        for (var index = 0; index < count; ++index)
        {
            SwfFrameLabel label;
            Call.Check(RadicleNative.SwfFrameLabelAt(use.Pointer, (nuint)index, &label));
            labels.Add(new SwfFrameLabelRecord(Utf8.Read(label.Name) ?? string.Empty, (int)label.TagIndex,
                                               label.SpriteId, label.InsideSprite != 0));
        }
        return labels;
    }

    private static IReadOnlyList<SwfAbcBlock> ReadAbcBlocks(SwfHandle handle, int count)
    {
        using var use = new HandleUse(handle, nameof(SwfFile));
        var blocks = new List<SwfAbcBlock>(count);
        for (var abc = 0; abc < count; ++abc)
        {
            SwfAbcInfo info;
            Call.Check(RadicleNative.SwfAbcInfoAt(use.Pointer, (nuint)abc, &info));
            var classes = new List<SwfClassSummary>();
            var classCount = (int)RadicleNative.SwfAbcClassCount(use.Pointer, (nuint)abc);
            for (var index = 0; index < classCount; ++index)
            {
                SwfClass klass;
                Call.Check(RadicleNative.SwfAbcClassAt(use.Pointer, (nuint)abc, (nuint)index, &klass));
                var interfaces = new List<string>((int)klass.InterfaceCount);
                for (var i = 0; i < (int)klass.InterfaceCount; ++i)
                {
                    IntPtr name;
                    Call.Check(RadicleNative.SwfAbcClassInterface(use.Pointer, (nuint)abc, (nuint)index, (nuint)i, &name));
                    interfaces.Add(Utf8.Read(name) ?? string.Empty);
                }
                var methods = new List<SwfMethodSummary>((int)klass.MethodCount);
                for (var m = 0; m < (int)klass.MethodCount; ++m)
                {
                    SwfMethod method;
                    Call.Check(RadicleNative.SwfAbcClassMethod(use.Pointer, (nuint)abc, (nuint)index, (nuint)m, &method));
                    methods.Add(ToSummary(method));
                }
                classes.Add(new SwfClassSummary(Utf8.Read(klass.Name) ?? string.Empty,
                                                Utf8.Read(klass.SuperName) ?? string.Empty, interfaces, methods));
            }

            var scripts = new List<SwfMethodSummary>();
            var scriptCount = (int)RadicleNative.SwfAbcScriptCount(use.Pointer, (nuint)abc);
            for (var index = 0; index < scriptCount; ++index)
            {
                SwfMethod method;
                Call.Check(RadicleNative.SwfAbcScriptAt(use.Pointer, (nuint)abc, (nuint)index, &method));
                scripts.Add(ToSummary(method));
            }

            var functions = new List<SwfMethodSummary>();
            var functionCount = (int)RadicleNative.SwfAbcFunctionCount(use.Pointer, (nuint)abc);
            for (var index = 0; index < functionCount; ++index)
            {
                SwfMethod method;
                Call.Check(RadicleNative.SwfAbcFunctionAt(use.Pointer, (nuint)abc, (nuint)index, &method));
                functions.Add(ToSummary(method));
            }

            blocks.Add(new SwfAbcBlock(Utf8.Read(info.Name) ?? string.Empty, info.Flags, (int)info.TagIndex,
                                       (long)info.PayloadSize, info.Parsed != 0, classes, scripts, functions));
        }
        return blocks;
    }

    private static SwfMethodSummary ToSummary(in SwfMethod method) => new(
        Utf8.Read(method.Name) ?? string.Empty,
        Utf8.Read(method.ReturnType) ?? string.Empty,
        method.Flags,
        (int)method.InstructionCount,
        (int)method.CallCount,
        (int)method.MultinameCount);

    private static SwfMethod ReadMethod(IntPtr swf, int abc, SwfOwner owner, int ownerIndex, int methodIndex)
    {
        SwfMethod method;
        var status = owner switch
        {
            SwfOwner.Class => RadicleNative.SwfAbcClassMethod(swf, (nuint)abc, (nuint)(int)ownerIndex, (nuint)methodIndex, &method),
            SwfOwner.Script => methodIndex == 0
                ? RadicleNative.SwfAbcScriptAt(swf, (nuint)abc, (nuint)(int)ownerIndex, &method)
                : throw new ArgumentOutOfRangeException(nameof(methodIndex)),
            SwfOwner.Function => methodIndex == 0
                ? RadicleNative.SwfAbcFunctionAt(swf, (nuint)abc, (nuint)(int)ownerIndex, &method)
                : throw new ArgumentOutOfRangeException(nameof(methodIndex)),
            _ => throw new ArgumentOutOfRangeException(nameof(owner)),
        };
        Call.Check(status);
        return method;
    }
}
