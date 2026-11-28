using System.IO;
using System.Text;
using Xunit;

namespace Radicle.Interop.Tests;

public class MaterialFileTests
{
    private static byte[] BgemV2(string baseTexture)
    {
        using var stream = new MemoryStream();
        using (var writer = new BinaryWriter(stream, Encoding.ASCII, leaveOpen: true))
        {
            void Text(string text)
            {
                var bytes = Encoding.ASCII.GetBytes(text + "\0");
                writer.Write((uint)bytes.Length);
                writer.Write(bytes);
            }

            writer.Write(0x4D454742u);
            writer.Write(2u);
            writer.Write(0u);
            for (var i = 0; i < 4; ++i) writer.Write(0f);
            writer.Write(1f);
            writer.Write((byte)0);
            writer.Write(0u);
            writer.Write(0u);
            writer.Write((byte)0);
            for (var i = 0; i < 11; ++i) writer.Write(false);
            writer.Write(0f);
            writer.Write(false);
            writer.Write(0f);
            writer.Write(false);

            Text(baseTexture);
            Text("Textures\\grey.dds");
            Text("Textures\\env.dds");
            Text("Textures\\normal.dds");
            writer.Write(0u);

            for (var i = 0; i < 6; ++i) writer.Write(false);
            for (var i = 0; i < 3; ++i) writer.Write(1f);
            writer.Write(1f);
            for (var i = 0; i < 4; ++i) writer.Write(0f);
            writer.Write(0.5f);
            writer.Write((byte)7);
            writer.Write(0.25f);
        }
        return stream.ToArray();
    }

    [Fact]
    public void BgemV2RoundTripsAndKeepsAnEditedBaseTexture()
    {
        var bytes = BgemV2("Textures\\old.dds");
        using var material = MaterialFile.FromBytes(bytes);
        Assert.Equal(MaterialFormat.Bgem, material.Format);
        Assert.Equal(2u, material.Version);
        Assert.Equal(bytes, material.ToBytes());
        Assert.Equal("Textures\\old.dds", material.GetText("BaseTexture"));
        Assert.Contains(material.Fields, field => field.Name == "BaseTexture" && field.Type == MaterialFieldType.String);

        material.SetText("BaseTexture", "Textures\\new.dds");
        var edited = material.ToBytes();
        using var reread = MaterialFile.FromBytes(edited);
        Assert.Equal(MaterialFormat.Bgem, reread.Format);
        Assert.Equal("Textures\\new.dds", reread.GetText("BaseTexture"));
        Assert.Equal(bytes.Length, edited.Length);
    }
}
