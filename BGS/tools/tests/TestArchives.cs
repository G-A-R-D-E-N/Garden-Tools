using System;
using System.IO;
using System.Text;

namespace BehaviourStudio.Tests;

internal static class TestArchives
{
    internal static void WriteGeneralBa2(string path, params (string Name, byte[] Payload)[] entries)
    {
        const int headerBytes = 24;
        const int recordBytes = 36;

        var names = new byte[entries.Length][];
        int payloadBytes = 0;
        for (int i = 0; i < entries.Length; i++)
        {
            names[i] = Encoding.UTF8.GetBytes(entries[i].Name);
            payloadBytes += entries[i].Payload.Length;
        }

        ulong dataStart = headerBytes + (ulong)(recordBytes * entries.Length);
        ulong nameTable = dataStart + (ulong)payloadBytes;

        using var stream = File.Create(path);
        using var writer = new BinaryWriter(stream);
        writer.Write(Encoding.ASCII.GetBytes("BTDX"));
        writer.Write((uint)1);
        writer.Write(Encoding.ASCII.GetBytes("GNRL"));
        writer.Write((uint)entries.Length);
        writer.Write(nameTable);

        ulong offset = dataStart;
        for (int i = 0; i < entries.Length; i++)
        {
            writer.Write((uint)0);
            writer.Write((uint)0);
            writer.Write((uint)0);
            writer.Write((uint)0x00100100);
            writer.Write(offset);
            writer.Write((uint)0);
            writer.Write((uint)entries[i].Payload.Length);
            writer.Write(0xBAADF00Du);
            offset += (ulong)entries[i].Payload.Length;
        }

        for (int i = 0; i < entries.Length; i++) writer.Write(entries[i].Payload);
        for (int i = 0; i < entries.Length; i++)
        {
            writer.Write((ushort)names[i].Length);
            writer.Write(names[i]);
        }
    }

    internal static void WriteBa2(string path, string entryName, byte[] payload) =>
        WriteGeneralBa2(path, (entryName, payload));

    internal sealed class TempDirectory : IDisposable
    {
        internal TempDirectory(string prefix) =>
            Path = Directory.CreateTempSubdirectory(prefix).FullName;

        internal string Path { get; }

        public void Dispose()
        {
            try { Directory.Delete(Path, recursive: true); }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
        }
    }
}
