using System;
using System.Buffers.Binary;
using System.IO;
using Radicle.Interop;
using Xunit;

namespace Radicle.Interop.Tests;

public class SoundAudioTests
{
    private static byte[] Wav(int channels, int rate, int bits, byte[] samples)
    {
        var wav = new byte[44 + samples.Length];
        "RIFF"u8.CopyTo(wav);
        BinaryPrimitives.WriteUInt32LittleEndian(wav.AsSpan(4), (uint)(36 + samples.Length));
        "WAVEfmt "u8.CopyTo(wav.AsSpan(8));
        BinaryPrimitives.WriteUInt32LittleEndian(wav.AsSpan(16), 16);
        BinaryPrimitives.WriteUInt16LittleEndian(wav.AsSpan(20), 1);
        BinaryPrimitives.WriteUInt16LittleEndian(wav.AsSpan(22), (ushort)channels);
        BinaryPrimitives.WriteUInt32LittleEndian(wav.AsSpan(24), (uint)rate);
        BinaryPrimitives.WriteUInt32LittleEndian(wav.AsSpan(28), (uint)(rate * channels * bits / 8));
        BinaryPrimitives.WriteUInt16LittleEndian(wav.AsSpan(32), (ushort)(channels * bits / 8));
        BinaryPrimitives.WriteUInt16LittleEndian(wav.AsSpan(34), (ushort)bits);
        "data"u8.CopyTo(wav.AsSpan(36));
        BinaryPrimitives.WriteUInt32LittleEndian(wav.AsSpan(40), (uint)samples.Length);
        samples.CopyTo(wav.AsSpan(44));
        return wav;
    }

    [Fact]
    public void DecodesA16BitStereoWavAndWritesItBackIdentically()
    {
        var source = Wav(2, 22050, 16, new byte[] { 1, 2, 3, 4, 5, 6, 7, 8 });
        var sound = SoundAudio.Decode(source);
        Assert.Equal(SoundContainer.Wav, sound.Container);
        Assert.Equal(22050, sound.SampleRate);
        Assert.Equal(2, sound.Channels);
        Assert.Equal(16, sound.BitsPerSample);
        Assert.Equal(0, sound.BadPackets);
        Assert.Equal(2, sound.FrameCount);
        Assert.Equal(new byte[] { 1, 2, 3, 4, 5, 6, 7, 8 }, sound.Pcm);
        Assert.Equal(source, sound.ToWav());
    }

    [Fact]
    public void KeepsEightBitSamplesAndReducesTwentyFourBitToSixteen()
    {
        var eight = SoundAudio.Decode(Wav(1, 11025, 8, new byte[] { 0, 128, 255 }));
        Assert.Equal(8, eight.BitsPerSample);
        Assert.Equal(new byte[] { 0, 128, 255 }, eight.Pcm);

        var wide = SoundAudio.Decode(Wav(1, 44100, 24, new byte[] { 1, 2, 3, 0x11, 0x12, 0x13 }));
        Assert.Equal(16, wide.BitsPerSample);
        Assert.Equal(new byte[] { 2, 3, 0x12, 0x13 }, wide.Pcm);
        Assert.Equal(2, wide.FrameCount);
    }

    [Fact]
    public void RejectsEmptyAndUnknownBytes()
    {
        Assert.Throws<ArgumentException>(() => SoundAudio.Decode(ReadOnlySpan<byte>.Empty));
        Assert.Throws<RadicleException>(() => SoundAudio.Decode(new byte[] { 1, 2, 3, 4, 5, 6, 7, 8 }));
    }

    [Fact]
    public void ReadResolvesALooseFileByStemFallbackAndAnswersNullWhenNothingMatches()
    {
        using var temp = new TempDirectory();
        temp.Write(Path.Combine("sound", "fx", "ui", "a.wav"), Wav(1, 8000, 16, new byte[] { 9, 8, 7, 6 }));
        using var assets = AssetResolver.Open(temp.Path, Array.Empty<string>());

        var found = SoundAudio.Read(assets, "ui\\a.xwm");
        Assert.NotNull(found);
        Assert.Equal("sound\\fx\\ui\\a.wav", found!.Path);
        Assert.Equal(8000, found.Sound.SampleRate);
        Assert.Equal(new byte[] { 9, 8, 7, 6 }, found.Sound.Pcm);

        Assert.Null(SoundAudio.Read(assets, "ui\\none.wav"));
    }

    [Fact]
    public void VanillaSoundsDecodeThroughTheResolverAsWavAndXwma()
    {
        var dataDir = Environment.GetEnvironmentVariable("FO4_DATA_DIR");
        if (string.IsNullOrEmpty(dataDir)) return;
        var archivePath = Path.Combine(dataDir, "Fallout4 - Sounds.ba2");
        if (!File.Exists(archivePath)) return;
        using var assets = AssetResolver.Open(dataDir, new[] { archivePath });
        string? wav = null, xwm = null;
        using (var archive = Ba2Archive.Open(archivePath))
        {
            foreach (var entry in archive.Entries)
            {
                if (wav is null && entry.Path.EndsWith(".wav", StringComparison.OrdinalIgnoreCase)) wav = entry.Path;
                if (xwm is null && entry.Path.EndsWith(".xwm", StringComparison.OrdinalIgnoreCase)) xwm = entry.Path;
                if (wav is not null && xwm is not null) break;
            }
        }
        Assert.NotNull(wav);
        Assert.NotNull(xwm);
        var plain = SoundAudio.Read(assets, "data\\" + wav);
        Assert.NotNull(plain);
        Assert.Equal(SoundContainer.Wav, plain!.Sound.Container);
        var compressed = SoundAudio.Read(assets, "data\\" + xwm![..^4] + ".wav");
        Assert.NotNull(compressed);
        Assert.Equal(SoundContainer.Xwma, compressed!.Sound.Container);
        foreach (var sound in new[] { plain.Sound, compressed.Sound })
        {
            Assert.InRange(sound.Channels, 1, 2);
            Assert.Contains(sound.BitsPerSample, new[] { 8, 16 });
            Assert.InRange(sound.SampleRate, 4000, 192000);
            Assert.NotEmpty(sound.Pcm);
        }
    }
}
