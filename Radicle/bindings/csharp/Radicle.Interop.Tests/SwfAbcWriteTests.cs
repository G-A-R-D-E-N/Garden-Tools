using System;
using System.Collections.Generic;
using System.IO;
using Radicle.Interop;
using Xunit;

namespace Radicle.Interop.Tests;

public partial class SwfTests
{
    private static byte[] MinimalAbc()
    {
        var bytes = new List<byte>();
        void U16(int value)
        {
            bytes.Add((byte)value);
            bytes.Add((byte)(value >> 8));
        }
        void U30(uint value)
        {
            do
            {
                var next = (byte)(value & 0x7F);
                value >>= 7;
                if (value != 0) next |= 0x80;
                bytes.Add(next);
            } while (value != 0);
        }
        U16(16);
        U16(46);
        U30(0);
        U30(0);
        U30(0);
        U30(2);
        U30(0);
        U30(3);
        bytes.Add((byte)'F');
        bytes.Add((byte)'o');
        bytes.Add((byte)'o');
        U30(0);
        U30(0);
        U30(0);
        U30(0);
        U30(0);
        U30(0);
        U30(0);
        U30(0);
        return bytes.ToArray();
    }

    private static byte[] AbcMovie(byte[] abc)
    {
        var payload = new List<byte>();
        payload.AddRange(BitConverter.GetBytes(0u));
        payload.AddRange("edit\0"u8.ToArray());
        payload.AddRange(abc);
        var body = new List<byte>();
        body.AddRange(Rect(0, 100, 0, 50));
        body.AddRange(BitConverter.GetBytes((ushort)0x0C00));
        body.AddRange(BitConverter.GetBytes((ushort)1));
        body.AddRange(Tag(82, payload.ToArray()));
        body.AddRange(Tag(0, Array.Empty<byte>()));
        var file = new List<byte> { (byte)'F', (byte)'W', (byte)'S', 10 };
        file.AddRange(BitConverter.GetBytes((uint)(body.Count + 8)));
        file.AddRange(body);
        return file.ToArray();
    }

    private static string EditFirstString(string text, string marker)
    {
        var line = text.IndexOf("  string ", StringComparison.Ordinal);
        if (line < 0) return string.Empty;
        var quote = text.IndexOf('"', line);
        var close = quote < 0 ? -1 : text.IndexOf('"', quote + 1);
        if (close < 0) return string.Empty;
        return text.Insert(close, marker);
    }

    [Fact]
    public void DumpsEditsAssemblesAndWritesAnAbcBlock()
    {
        var abc = MinimalAbc();
        using var swf = SwfFile.FromBytes(AbcMovie(abc));
        Assert.Single(swf.AbcBlocks);

        var text = swf.AbcText(0);
        Assert.Contains("abc 46.16", text);
        Assert.Equal(abc, swf.AssembleAbc(0, text));

        var edited = EditFirstString(text, "CSharpEdit");
        Assert.False(string.IsNullOrEmpty(edited));
        swf.ApplyAbcText(0, edited);
        var bytes = swf.WriteBytes();
        Assert.NotEmpty(bytes);

        using var reopened = SwfFile.FromBytes(bytes);
        Assert.Contains("CSharpEdit", reopened.AbcText(0));
    }

    [Fact]
    public void RealInterfaceMovieRoundTripsAnAbcEdit()
    {
        var dataDir = Environment.GetEnvironmentVariable("FO4_DATA_DIR");
        if (string.IsNullOrEmpty(dataDir)) return;
        var archivePath = Path.Combine(dataDir, "Fallout4 - Interface.ba2");
        if (!File.Exists(archivePath)) return;
        using var archive = Ba2Archive.Open(archivePath);
        foreach (var entry in archive.Entries)
        {
            if (!entry.Path.EndsWith(".swf", StringComparison.OrdinalIgnoreCase)) continue;
            using var swf = SwfFile.FromArchive(archive, entry.Path);
            if (swf.AbcBlocks.Count == 0) continue;
            var text = swf.AbcText(0);
            var edited = EditFirstString(text, "CSharpRealEdit");
            Assert.False(string.IsNullOrEmpty(edited));
            swf.ApplyAbcText(0, edited);
            var bytes = swf.WriteBytes();
            using var reopened = SwfFile.FromBytes(bytes);
            Assert.Contains("CSharpRealEdit", reopened.AbcText(0));
            return;
        }
    }
}
