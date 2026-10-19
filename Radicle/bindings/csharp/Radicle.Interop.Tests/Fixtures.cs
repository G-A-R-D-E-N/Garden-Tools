using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace Radicle.Interop.Tests;

// source:Radicle/src/plugin/reader.cpp; fixture records and groups must use the layout it parses.
internal abstract record Node;

internal sealed record Rec(string Signature, uint FormId, (string Sig, byte[] Data)[] Subs, uint Flags = 0, ushort FormVersion = 131) : Node;

internal sealed record Grp(uint Label, int Type, Node[] Children) : Node;

internal static class Bytes
{
    public static byte[] U32(params uint[] values) => values.SelectMany(BitConverter.GetBytes).ToArray();
    public static byte[] F32(params float[] values) => values.SelectMany(BitConverter.GetBytes).ToArray();
    public static byte[] Z(string text) => Encoding.ASCII.GetBytes(text + "\0");
    public static uint Label(string signature) => BitConverter.ToUInt32(Encoding.ASCII.GetBytes(signature));
}

internal static class PluginWriter
{
    public static Grp Top(string signature, params Node[] children) => new(Bytes.Label(signature), 0, children);

    public static byte[] Build(uint flags, string[] masters, params Node[] groups) => Build(flags, 1.0f, masters, groups);

    public static byte[] Build(uint flags, float version, string[] masters, Node[] groups)
    {
        using var stream = new MemoryStream();
        using var w = new BinaryWriter(stream);
        var tes4 = new List<(string, byte[])> { ("HEDR", Bytes.F32(version).Concat(Bytes.U32(0, 0x900)).ToArray()) };
        foreach (var master in masters)
        {
            tes4.Add(("MAST", Bytes.Z(master)));
            tes4.Add(("DATA", Bytes.U32(0, 0)));
        }
        WriteRecord(w, new Rec("TES4", 0, tes4.ToArray(), flags));
        foreach (var group in groups) WriteNode(w, group);
        return stream.ToArray();
    }

    private static void WriteNode(BinaryWriter w, Node node)
    {
        if (node is Rec rec)
        {
            WriteRecord(w, rec);
            return;
        }
        var grp = (Grp)node;
        using var inner = new MemoryStream();
        using (var iw = new BinaryWriter(inner))
        {
            foreach (var child in grp.Children) WriteNode(iw, child);
        }
        var body = inner.ToArray();
        w.Write(Encoding.ASCII.GetBytes("GRUP"));
        w.Write((uint)(24 + body.Length));
        w.Write(grp.Label);
        w.Write(grp.Type);
        w.Write(0u);
        w.Write(0u);
        w.Write(body);
    }

    private static void WriteRecord(BinaryWriter w, Rec rec)
    {
        var payload = rec.Subs.SelectMany(s =>
            Encoding.ASCII.GetBytes(s.Sig).Concat(BitConverter.GetBytes((ushort)s.Data.Length)).Concat(s.Data)).ToArray();
        w.Write(Encoding.ASCII.GetBytes(rec.Signature));
        w.Write((uint)payload.Length);
        w.Write(rec.Flags);
        w.Write(rec.FormId);
        w.Write(0u);
        // source:Radicle/include/fo4/forms/weather.hpp; WTHR subrecord sizes must match this header form version.
        w.Write(rec.FormVersion);
        w.Write((ushort)0);
        w.Write(payload);
    }
}

internal static class StringsWriter
{
    /// <summary>A .STRINGS table: count, data size, (id, offset) rows, NUL-terminated text.</summary>
    public static byte[] Table(params (uint Id, string Text)[] rows)
    {
        using var data = new MemoryStream();
        var offsets = new List<uint>();
        foreach (var (_, text) in rows)
        {
            offsets.Add((uint)data.Length);
            data.Write(Bytes.Z(text));
        }
        using var stream = new MemoryStream();
        using var w = new BinaryWriter(stream);
        w.Write((uint)rows.Length);
        w.Write((uint)data.Length);
        for (var i = 0; i < rows.Length; ++i)
        {
            w.Write(rows[i].Id);
            w.Write(offsets[i]);
        }
        w.Write(data.ToArray());
        return stream.ToArray();
    }
}

public sealed class TempDirectory : IDisposable
{
    public TempDirectory()
    {
        Path = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "radicle_cs_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path);
    }

    public string Path { get; }

    public string Write(string relative, byte[] bytes)
    {
        var full = System.IO.Path.Combine(Path, relative);
        Directory.CreateDirectory(System.IO.Path.GetDirectoryName(full)!);
        File.WriteAllBytes(full, bytes);
        return full;
    }

    public void Dispose()
    {
        try { Directory.Delete(Path, recursive: true); } catch (IOException) { }
    }
}
