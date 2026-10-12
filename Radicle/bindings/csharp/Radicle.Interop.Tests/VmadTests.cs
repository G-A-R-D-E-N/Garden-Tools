using System;
using System.IO;
using System.Text;
using Xunit;

namespace Radicle.Interop.Tests;

public sealed class VmadTests
{
    [Theory]
    [InlineData(16, 0, false)]
    [InlineData(16, 1, true)]
    [InlineData(6, 0, true)]
    public void VariableArrayCoverageAndUnchangedSave(byte type, uint count, bool unsupported)
    {
        using var stream = new MemoryStream();
        using (var writer = new BinaryWriter(stream, Encoding.UTF8, leaveOpen: true))
        {
            void Name(string text)
            {
                var bytes = Encoding.UTF8.GetBytes(text);
                writer.Write((ushort)bytes.Length);
                writer.Write(bytes);
            }
            writer.Write((short)6);
            writer.Write((short)2);
            writer.Write((ushort)1);
            Name("VariableScript");
            writer.Write((byte)0);
            writer.Write((ushort)3);
            Name("Before");
            writer.Write((byte)3);
            writer.Write((byte)1);
            writer.Write(11);
            Name("DynamicValue");
            writer.Write(type);
            writer.Write((byte)1);
            if (type == 16) writer.Write(count);
            if (count != 0) writer.Write(0x0200009A);
            Name("After");
            writer.Write((byte)3);
            writer.Write((byte)1);
            writer.Write(13);
        }
        using var temp = new TempDirectory();
        var bytesOnDisk = PluginWriter.Build(0, Array.Empty<string>(),
            PluginWriter.Top("REFR", new Rec("REFR", 0x800, new[] { ("VMAD", stream.ToArray()) })));
        var path = temp.Write("Variable.esp", bytesOnDisk);
        using var plugin = PluginFile.Read(path);
        Assert.Equal(bytesOnDisk, plugin.ToBytes());
        var saved = Path.Combine(temp.Path, "Saved.esp");
        plugin.Write(saved);
        Assert.Equal(bytesOnDisk, File.ReadAllBytes(saved));
        using var env = RadicleEnvironment.Load(new[] { saved });
        var record = env.Winner(new FormKey("Saved.esp", 0x800))!.Value;
        var decoded = env.PlacedReference(record);
        Assert.Equal(unsupported, decoded.ScriptsUnsupported);
        var properties = Assert.Single(decoded.Scripts).Properties;
        Assert.Equal(11, properties[0].Integer);
        Assert.Equal(unsupported ? 1 : 3, properties.Count);
        if (!unsupported)
        {
            Assert.Equal((byte)16, properties[1].Type);
            Assert.Equal(0u, properties[1].ElementCount);
            Assert.Equal(13, properties[2].Integer);
        }
    }
}
