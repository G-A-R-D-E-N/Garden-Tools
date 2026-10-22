using System.Text;
using OpenCommonwealth.Services.Archive;
using Xunit;

namespace BehaviourStudio.Tests;

[Collection("ScanCli")]
public sealed class GameDataSharedPathTests
{
    [Fact]
    public void SubgraphIndexReadsEveryManifestFromOneOpenArchive()
    {
        string root = TempRoot();
        string dataFolder = Path.Combine(root, "Data");
        Directory.CreateDirectory(dataFolder);
        try
        {
            int[] ids = { 101, 102, 103 };
            WriteGeneralBa2(Path.Combine(dataFolder, "A.ba2"),
                ids.Select(id => ($"Meshes/AnimTextData/AnimationFileData/{id}.txt", Manifest(id))).ToArray());

            using var data = GameData.Discover(dataFolder, Path.Combine(root, "plugins.txt"));

            Ba2.OpenCountForTest = 0;
            var index = SubgraphIndex.Discover(data);

            Assert.Equal(ids.Length, index.Subgraphs.Count);
            Assert.Equal(1, Ba2.OpenCountForTest);
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void SubgraphIndexFindsLooseManifestUnderLowercaseAnimTextData()
    {
        string root = TempRoot();
        string dataFolder = Path.Combine(root, "Data");
        string modRoot = Path.Combine(root, "mods", "LowerAnim");
        const ulong id = 1234567890123;
        string fileData = Path.Combine(modRoot, "meshes", "animtextdata", "animationfiledata");
        string offsets = Path.Combine(modRoot, "meshes", "animtextdata", "animationoffsets");
        Directory.CreateDirectory(dataFolder);
        Directory.CreateDirectory(fileData);
        Directory.CreateDirectory(offsets);
        try
        {
            File.WriteAllText(Path.Combine(fileData, $"{id}.txt"),
                "V1\nx\n1234567890123\nx\nActors\\Test\\Behaviors\\Test.hkx\n");
            File.WriteAllText(Path.Combine(offsets, $"{id}.txt"), "V4\n");

            using var data = GameData.Discover(dataFolder, Path.Combine(root, "plugins.txt"), new[] { modRoot });
            var index = SubgraphIndex.Discover(data);

            Assert.Equal("Actors\\Test\\Behaviors\\Test.hkx", index.Find(id)?.PrimaryBehavior);
            Assert.NotNull(index.FindOffsetData(id));
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    private static byte[] Manifest(int id) =>
        Encoding.UTF8.GetBytes($"V4\nTest{id}\n{id}\n0\nActors\\Test{id}\\Behaviors\\Test.hkx\n");

    private static string TempRoot() => Path.Combine(Path.GetTempPath(), "bgs-shared-path-" + Guid.NewGuid().ToString("N"));

    private static void WriteGeneralBa2(string path, params (string Name, byte[] Payload)[] files)
    {
        const int headerBytes = 24;
        const int recordBytes = 36;
        ulong nameTable = (ulong)(headerBytes + recordBytes * files.Length);
        ulong dataOffset = nameTable + files.Aggregate(0UL, (sum, f) => sum + 2UL + (ulong)Encoding.UTF8.GetByteCount(f.Name));

        using var stream = File.Create(path);
        using var writer = new BinaryWriter(stream);
        writer.Write(Encoding.ASCII.GetBytes("BTDX"));
        writer.Write((uint)1);
        writer.Write(Encoding.ASCII.GetBytes("GNRL"));
        writer.Write((uint)files.Length);
        writer.Write(nameTable);
        foreach (var (_, payload) in files)
        {
            writer.Write((uint)0);
            writer.Write((uint)0);
            writer.Write((uint)0);
            writer.Write((uint)0x00100100);
            writer.Write(dataOffset);
            writer.Write((uint)0);
            writer.Write((uint)payload.Length);
            writer.Write(0xBAADF00Du);
            dataOffset += (ulong)payload.Length;
        }
        foreach (var (name, _) in files)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(name);
            writer.Write((ushort)bytes.Length);
            writer.Write(bytes);
        }
        foreach (var (_, payload) in files) writer.Write(payload);
    }
}
