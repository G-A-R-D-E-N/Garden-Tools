using System;
using System.Linq;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class SoundRecordTests
{
    [Fact]
    public void MissingKindUsesValuesLayoutAndStrayFilenameFailsClosed()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Sound.esm", Build(1, Array.Empty<string>(), Top("SNDR",
            new Rec("SNDR", 0x800, new[] { ("LNAM", U32(0)), ("LNAM", U32(0x44332211)),
                ("BNAM", new byte[] { 0xF6, 5, 200, 3, 0x10, 0x27 }) }),
            new Rec("SNDR", 0x801, new[] { ("CNAM", U32(0x1EEF540A)), ("FNAM", U32(0x06000010)) }))));
        using var env = RadicleEnvironment.Load(new[] { path });
        var descriptor = env.SoundDescriptor(env.Winner(new FormKey("Sound.esm", 0x800))!.Value);
        Assert.Null(descriptor.Kind);
        Assert.Equal(new byte[] { 0x11, 0x22, 0x33, 0x44 }, descriptor.Controls);
        Assert.Equal(new SoundPlayback(-10, 5, 200, 3, 10000), descriptor.Playback);
        Assert.Throws<RadicleException>(() => env.SoundDescriptor(env.Winner(new FormKey("Sound.esm", 0x801))!.Value));
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void MarkerAndStandardDescriptorExposeStoredValues(bool stackable)
    {
        using var temp = new TempDirectory();
        var repeat = F32(2.5f, 8f);
        if (stackable) repeat = repeat.Concat(new byte[] { 0x80 }).ToArray();
        var path = temp.Write("Sound.esm", Build(1, Array.Empty<string>(),
            Top("SNDR", new Rec("SNDR", 0x800, new[]
            {
                ("CNAM", U32(0x1EEF540A)), ("BNAM", new byte[] { 0xFE, 0xFD, 7, 9, 0x34, 0x12 }),
                ("LNAM", new byte[] { 0x11, 0x22, 0x33, 0x44 }), ("GNAM", U32(0x810)),
                ("SNAM", U32(0x811)), ("ONAM", U32(0x812)), ("ANAM", Z(@"sound\one.xwm")),
                ("ANAM", Z(@"sound\two.wav")),
            })), Top("SOUN", new Rec("SOUN", 0x801, new[] { ("SDSC", U32(0x800)), ("REPT", repeat) }))));
        using var env = RadicleEnvironment.Load(new[] { path });
        Record R(uint id) => env.Winner(new FormKey("Sound.esm", id))!.Value;
        var marker = env.SoundMarker(R(0x801));
        Assert.Equal(new FormKey("Sound.esm", 0x800), marker.Descriptor);
        Assert.Equal(2.5f, marker.Repeat!.MinTime);
        Assert.Equal(8f, marker.Repeat.MaxTime);
        Assert.Equal(stackable ? (byte?)0x80 : null, marker.Repeat.Stackable);
        var d = env.SoundDescriptor(env.Winner(marker.Descriptor!.Value)!.Value);
        Assert.Equal((uint?)0x1EEF540Au, d.Kind);
        Assert.Equal(new FormKey("Sound.esm", 0x810), d.Category);
        Assert.Equal(new FormKey("Sound.esm", 0x811), d.Alternate);
        Assert.Equal(new FormKey("Sound.esm", 0x812), d.OutputModel);
        Assert.Equal(new[] { @"sound\one.xwm", @"sound\two.wav" }, d.Files);
        Assert.Equal(new byte[] { 0x11, 0x22, 0x33, 0x44 }, d.Controls);
        Assert.Equal(new SoundPlayback(-2, -3, 7, 9, 0x1234), d.Playback);
        Assert.Null(d.BaseDescriptor);
        Assert.Empty(d.Descriptors);
        Assert.Empty(d.RateFiles);
    }

    [Fact]
    public void CompoundAndAutoWeaponExposeChildLinksAndRateFiles()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Sound.esm", Build(1, Array.Empty<string>(), Top("SNDR",
            new Rec("SNDR", 0x800, new[] { ("CNAM", U32(0x54651A43)), ("DNAM", U32(0x810)), ("DNAM", U32(0x811)) }),
            new Rec("SNDR", 0x801, new[]
            {
                ("CNAM", U32(0xED157AE3)), ("BNAM", U32(0x812)), ("ITMC", U32(1)),
                ("ITMS", Array.Empty<byte>()), ("INTV", U32(750)), ("FNAM", Z(@"sound\auto.xwm")),
                ("ITME", Array.Empty<byte>()),
            }))));
        using var env = RadicleEnvironment.Load(new[] { path });
        var compound = env.SoundDescriptor(env.Winner(new FormKey("Sound.esm", 0x800))!.Value);
        Assert.Equal(new FormKey?[] { new("Sound.esm", 0x810), new("Sound.esm", 0x811) }, compound.Descriptors);
        Assert.Null(compound.Playback);
        Assert.Null(compound.Controls);
        var weapon = env.SoundDescriptor(env.Winner(new FormKey("Sound.esm", 0x801))!.Value);
        Assert.Equal(new FormKey("Sound.esm", 0x812), weapon.BaseDescriptor);
        Assert.Equal(new SoundRateFile(750, @"sound\auto.xwm"), Assert.Single(weapon.RateFiles));
        Assert.Null(weapon.Playback);
    }

    [Fact]
    public void MusicAndFootstepsPreserveRawFlagsLinksAndCategoryOrder()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Sound.esm", Build(1, Array.Empty<string>(),
            Top("MUSC", new Rec("MUSC", 0x800, new[]
            {
                ("FNAM", U32(0x80000041)), ("PNAM", U32(0xABCD1234)), ("WNAM", F32(3.25f)), ("TNAM", U32(0x810, 0)),
            })), Top("FSTP", new Rec("FSTP", 0x801, new[] { ("DATA", U32(0x820)), ("ANAM", Z("FootLeft")) })),
            Top("FSTS", new Rec("FSTS", 0x802, new[]
            {
                ("XCNT", U32(1, 2, 0, 1, 1)), ("DATA", U32(0x815, 0x814, 0x812, 0x813, 0x811)),
            }))));
        using var env = RadicleEnvironment.Load(new[] { path });
        Record R(uint id) => env.Winner(new FormKey("Sound.esm", id))!.Value;
        var music = env.MusicType(R(0x800));
        Assert.Equal(0x80000041u, music.Flags);
        Assert.Equal((ushort)0x1234, music.Priority);
        Assert.Equal((ushort)0xABCD, music.DuckingHundredths);
        Assert.Equal(3.25f, music.FadeDuration);
        Assert.Equal(new FormKey?[] { new("Sound.esm", 0x810), null }, music.Tracks);
        Assert.Equal(new Footstep(new("Sound.esm", 0x820), "FootLeft"), env.Footstep(R(0x801)));
        var set = env.FootstepSet(R(0x802));
        Assert.Equal(new FormKey("Sound.esm", 0x811), Assert.Single(set.Walking));
        Assert.Equal(new FormKey?[] { new("Sound.esm", 0x812), new("Sound.esm", 0x813) }, set.Running);
        Assert.Empty(set.Sprinting);
        Assert.Equal(new FormKey("Sound.esm", 0x814), Assert.Single(set.Sneaking));
        Assert.Equal(new FormKey("Sound.esm", 0x815), Assert.Single(set.Swimming));
    }

    [Fact]
    public void MarkerResolvesThroughCompoundToEachStandardDescriptorsFiles()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Sound.esm", Build(1, Array.Empty<string>(),
            Top("SNDR",
                new Rec("SNDR", 0x800, new[] { ("CNAM", U32(0x54651A43)), ("DNAM", U32(0x810)), ("DNAM", U32(0x811)) }),
                new Rec("SNDR", 0x810, new[] { ("CNAM", U32(0x1EEF540A)), ("ANAM", Z(@"sound\one.xwm")) }),
                new Rec("SNDR", 0x811, new[] { ("CNAM", U32(0x1EEF540A)), ("ANAM", Z(@"sound\two.wav")) })),
            Top("SOUN", new Rec("SOUN", 0x801, new[] { ("SDSC", U32(0x800)) }))));
        using var env = RadicleEnvironment.Load(new[] { path });
        var marker = env.Winner(new FormKey("Sound.esm", 0x801))!.Value;
        Assert.Equal(new[] { @"sound\one.xwm", @"sound\two.wav" }, env.SoundFiles(marker));
    }

    [Fact]
    public void RateAndStandardChoosersFollowTheDeterministicRules()
    {
        var rates = new[] { new SoundRateFile(0, "zero.wav"), new SoundRateFile(420, "a.wav"), new SoundRateFile(540, "b.wav") };
        Assert.Equal((int?)1, RadicleEnvironment.SelectRateFileIndex(rates, 479));
        Assert.Equal((int?)2, RadicleEnvironment.SelectRateFileIndex(rates, 480));
        Assert.Equal((int?)2, RadicleEnvironment.SelectRateFileIndex(rates, 1000));
        Assert.Null(RadicleEnvironment.SelectRateFileIndex(new[] { new SoundRateFile(0, "zero.wav") }, 500));
        Assert.Equal(0, RadicleEnvironment.SelectStandardFileIndex(1, 0, null));
        Assert.Equal(0, RadicleEnvironment.SelectStandardFileIndex(1, 0, 0));
        Assert.Equal(1, RadicleEnvironment.SelectStandardFileIndex(3, 0, 0));
        Assert.Equal(0, RadicleEnvironment.SelectStandardFileIndex(3, 2, 2));
        Assert.Throws<RadicleException>(() => RadicleEnvironment.SelectStandardFileIndex(1, 1, null));
    }

    [Fact]
    public void DerivedControlsUseTheirBoundaryValues()
    {
        Assert.Equal(2.0, RadicleEnvironment.SoundFrequencyFactor(127));
        Assert.Equal(0.1, RadicleEnvironment.SoundFrequencyFactor(-100));
        Assert.True(RadicleEnvironment.SoundIsLooping(0x20));
        Assert.False(RadicleEnvironment.SoundIsLooping(0x40));
        Assert.Equal(1.0, RadicleEnvironment.SoundSidechainSend(255));
        Assert.Equal(1.0, RadicleEnvironment.SoundSmallRumbleSend(0x0F));
        Assert.Equal(1.0, RadicleEnvironment.SoundBigRumbleSend(0xF0));
    }
}
