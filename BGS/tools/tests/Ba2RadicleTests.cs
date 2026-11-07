using System;
using System.IO;
using System.Text;
using BehaviourStudio.App;
using OpenCommonwealth.Services.Archive;
using Radicle.Interop;
using Xunit;

namespace BehaviourStudio.Tests;

[Collection("ScanCli")]
public sealed class Ba2RadicleTests
{
    [Fact]
    public void NativeLoadFailureIncludesRadicleAbiMismatch()
    {
        var mismatch = new RadicleAbiMismatchException("stale radicle.dll");
        Assert.True(Ba2.IsNativeLoadFailure(mismatch));
        Assert.True(Ba2.IsNativeLoadFailure(new TypeInitializationException("Ba2Archive", mismatch)));
        Assert.False(Ba2.IsNativeLoadFailure(new InvalidOperationException("unrelated")));
    }

    [Fact]
    public void GameDataArchiveFilterPropagatesWrappedRadicleAbiMismatch()
    {
        var wrapped = new InvalidDataException("wrapped", new RadicleAbiMismatchException("stale radicle.dll"));
        Assert.False(GameData.IsSkippableArchiveFailure(wrapped));
        Assert.True(GameData.IsSkippableArchiveFailure(new InvalidDataException("unreadable archive")));
    }

    [Fact]
    public void PlaybackSkeletonLookupReportsNativeLoadFailureButSkipsCorruptArchives()
    {
        var native = new InvalidDataException("the Radicle native library could not be loaded",
            new DllNotFoundException("radicle_c"));
        Assert.Same(native, Assert.Throws<InvalidDataException>(
            () => MainWindow.SkeletonBytesOrNull(() => throw native)));
        Assert.Null(MainWindow.SkeletonBytesOrNull(() => throw new InvalidDataException("corrupt archive")));
        Assert.Null(MainWindow.SkeletonBytesOrNull(() => throw new IOException("truncated archive")));
    }

    [Fact]
    public void Ba2WrapperReadsGeneralArchivesThroughRadicleAndRefusesTextureArchives()
    {
        string root = Path.Combine(Path.GetTempPath(), "bgs-ba2-radicle-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            byte[] payload = Encoding.ASCII.GetBytes("tiny hkx");
            string archive = Path.Combine(root, "tiny.ba2");
            TestArchives.WriteGeneralBa2(archive, ("meshes\\actors/tiny.hkx", payload));

            using (var ba2 = Ba2.Open(archive))
            {
                var entry = Assert.Single(ba2.Entries);
                Assert.Equal("meshes/actors/tiny.hkx", entry.Name);
                Assert.Equal("tiny.hkx", entry.FileName);
                Assert.Equal(payload, ba2.Read(entry));
            }

            string output = Path.Combine(root, "out");
            Assert.Equal(1, Ba2.ExtractMatching(archive, "tiny", output, ".hkx", _ => { }, keepFolders: true));
            Assert.Equal(payload, File.ReadAllBytes(Path.Combine(output, "meshes", "actors", "tiny.hkx")));

            string textures = Path.Combine(root, "textures.ba2");
            using (var stream = File.Create(textures))
            using (var writer = new BinaryWriter(stream))
            {
                writer.Write(Encoding.ASCII.GetBytes("BTDX"));
                writer.Write((uint)1);
                writer.Write(Encoding.ASCII.GetBytes("DX10"));
                writer.Write((uint)0);
                writer.Write((ulong)0);
            }
            var error = Assert.Throws<InvalidDataException>(() => Ba2.Open(textures));
            Assert.Equal("textures.ba2 is a DX10 archive, not GNRL. Textures are stored differently and nothing here reads them.",
                         error.Message);
        }
        finally { Directory.Delete(root, recursive: true); }
    }
}
