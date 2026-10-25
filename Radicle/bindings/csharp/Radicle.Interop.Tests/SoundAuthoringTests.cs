using System;
using System.IO;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class SoundAuthoringTests
{
    private const string Plugin = "Snd.esm";

    private static Radicle.Interop.FormKey K(uint id) => new(Plugin, id);

    private static readonly Radicle.Interop.FormKey Std = K(0x800), Comp = K(0x801), Auto = K(0x802), NoKind = K(0x803), Bare = K(0x804),
        Marker = K(0x805), Music = K(0x807), Step = K(0x808), Set = K(0x809), Must = K(0x812), Misc = K(0x814), Step2 = K(0x815);

    private static byte[] Source()
    {
        var edid = ("EDID", Z("Other"));
        return Build(1, Array.Empty<string>(),
            Top("SNDR",
                new Rec("SNDR", 0x800, new[] { ("EDID", Z("Std")), ("CNAM", U32(0x1EEF540A)), ("GNAM", U32(0x810)), ("ANAM", Z("a.wav")),
                    ("ANAM", Z("b.wav")), ("ONAM", U32(0x811)), ("LNAM", new byte[] { 1, 8, 2, 3 }), ("BNAM", new byte[] { 0xFE, 3, 50, 4, 0x10, 0x27 }) }),
                new Rec("SNDR", 0x801, new[] { ("EDID", Z("Comp")), ("CNAM", U32(0x54651A43)), ("DNAM", U32(0x800)), ("DNAM", U32(0x803)) }),
                new Rec("SNDR", 0x802, new[] { ("EDID", Z("Auto")), ("CNAM", U32(0xED157AE3)), ("BNAM", U32(0x800)), ("ITMC", U32(1)),
                    ("ITMS", Array.Empty<byte>()), ("INTV", U32(600)), ("FNAM", Z("r1.wav")), ("ITME", Array.Empty<byte>()) }),
                new Rec("SNDR", 0x803, new[] { ("EDID", Z("NoKind")), ("BNAM", new byte[6]) }),
                new Rec("SNDR", 0x804, new[] { ("EDID", Z("Bare")) })),
            Top("SOUN", new Rec("SOUN", 0x805, new[] { ("EDID", Z("Marker")), ("OBND", new byte[12]), ("SDSC", U32(0x800)),
                ("REPT", F32(1f, 2f).Concat(new byte[] { 1 }).ToArray()) })),
            Top("MUSC", new Rec("MUSC", 0x807, new[] { ("EDID", Z("Music")), ("FNAM", U32(0x41)), ("PNAM", new byte[] { 50, 0, 0, 0 }),
                ("WNAM", F32(3f)), ("TNAM", U32(0x812, 0)) })),
            Top("FSTP", new Rec("FSTP", 0x808, new[] { ("EDID", Z("Step")), ("DATA", U32(0x813)), ("ANAM", Z("Left")) }),
                new Rec("FSTP", 0x815, new[] { ("EDID", Z("Step2")), ("DATA", U32(0)), ("ANAM", Z("Right")) })),
            Top("FSTS", new Rec("FSTS", 0x809, new[] { ("EDID", Z("Set")), ("XCNT", U32(1, 2, 0, 1, 1)), ("DATA", U32(0x808, 0x808, 0x808, 0x815, 0x808)) })),
            Top("SNCT", new Rec("SNCT", 0x810, new[] { edid })),
            Top("SOPM", new Rec("SOPM", 0x811, new[] { edid })),
            Top("MUST", new Rec("MUST", 0x812, new[] { edid })),
            Top("IPDS", new Rec("IPDS", 0x813, new[] { edid })),
            Top("MISC", new Rec("MISC", 0x814, new[] { edid })));
    }

    private static Status Refusal(Action action) => Assert.Throws<RadicleException>(action).Status;

    [Fact]
    public void StructureEditsAddRemoveFilesSetTheBaseLinkAndCreateRecords()
    {
        using var temp = new TempDirectory();
        using var plugin = PluginFile.Read(temp.Write("Snd.esp", Source()));
        var added = plugin.AddSoundFile(Std, "c.wav");
        plugin.SetSoundFile(Std, added, "d.wav");
        plugin.AddSoundFile(Bare, "e.wav");
        plugin.RemoveSoundFile(Bare, 0);
        plugin.AddSoundBaseLink(Auto, Comp);
        plugin.RemoveSoundBaseLink(Auto);
        plugin.AddSoundBaseLink(Auto, Comp);
        var marker = plugin.CreateSoundRecord("SOUN", "NewMarker");
        var steps = plugin.CreateSoundRecord("FSTS", "NewSteps");
        Assert.Throws<RadicleException>(() => plugin.CreateSoundRecord("MISC", "Nope"));
        Assert.Throws<RadicleException>(() => plugin.AddSoundBaseLink(Std, Auto));
        Assert.Throws<RadicleException>(() => plugin.RemoveSoundBaseLink(Std));
        Assert.Throws<RadicleException>(() => plugin.RemoveSoundFile(Std, 99));
        Assert.Throws<ArgumentException>(() => plugin.AddSoundFile(Std, "a\0b"));
        var path = Path.Combine(temp.Path, "Saved.esp");
        plugin.Write(path);
        using var env = RadicleEnvironment.Load(new[] { path });
        var descriptor = env.SoundDescriptor(env.Winner(new Radicle.Interop.FormKey("Saved.esp", 0x800))!.Value);
        Assert.Contains("d.wav", descriptor.Files);
        var autoDescriptor = env.SoundDescriptor(env.Winner(new Radicle.Interop.FormKey("Saved.esp", 0x802))!.Value);
        Assert.Equal(new Radicle.Interop.FormKey("Saved.esp", 0x801), autoDescriptor.BaseDescriptor);
        Assert.NotEqual(0u, marker.LocalId);
        Assert.NotEqual(0u, steps.LocalId);
    }

    [Fact]
    public void DescriptorFieldsFollowTheKindAndSurviveReload()
    {
        using var temp = new TempDirectory();
        var path = temp.Write(Plugin, Source());
        using var plugin = PluginFile.Read(path);

        Assert.True(plugin.SetScalar(Std, "SNDR\\FrequencyShift", "-10"));
        Assert.True(plugin.SetScalar(Std, "SNDR\\StaticAttenuation", "250"));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetScalar(Std, "SNDR\\FrequencyShift", "128")));
        Assert.False(plugin.SetScalar(Auto, "SNDR\\Priority", "1"));
        Assert.True(plugin.SetScalar(NoKind, "SNDR\\Priority", "77"));
        Assert.True(plugin.SetFormLink(Auto, "SNDR\\BaseDescriptor", Comp));
        Assert.False(plugin.SetFormLink(Std, "SNDR\\BaseDescriptor", Comp));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetFormLink(Auto, "SNDR\\BaseDescriptor", null)));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetFormLink(Std, "SNDR\\Category", Misc)));
        Assert.Equal(2, plugin.FieldCount(Comp, "SNDR\\Descriptors"));
        Assert.True(plugin.SetFormLink(Comp, "SNDR\\Descriptors", Auto, 1));
        Assert.Equal(2, plugin.FieldCount(Std, "SNDR\\Sounds"));
        Assert.True(plugin.SetString(Std, "SNDR\\Sounds", "sound\\c.xwm", 1));
        Assert.True(plugin.SetScalar(Auto, "SNDR\\RateOfFireRpm", "1200"));
        Assert.True(plugin.SetString(Auto, "SNDR\\RateOfFireFile", "sound\\fast.xwm"));
        Assert.True(plugin.AddSubrecord(Bare, "NNAM"));
        Assert.True(plugin.AddSubrecord(Bare, "LNAM"));
        Assert.True(plugin.SetString(Bare, "SNDR\\Notes", "note"));
        Assert.True(plugin.SetScalar(Bare, "SNDR\\Looping", "8"));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.AddSubrecord(Bare, "CNAM")));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.AddSubrecord(Bare, "GNAM")));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.AddSubrecord(Auto, "NNAM")));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.RemoveSubrecord(Std, "BNAM")));
        Assert.True(plugin.RemoveSubrecord(Std, "ANAM", 0));
        var edited = plugin.ToBytes();
        plugin.Write(path);

        using var env = RadicleEnvironment.Load(new[] { path });
        var std = env.SoundDescriptor(env.Winner(Std)!.Value);
        Assert.Equal(new SoundPlayback(-10, 3, 50, 4, 250), std.Playback);
        Assert.Equal(new[] { "sound\\c.xwm" }, std.Files);
        var noKind = env.SoundDescriptor(env.Winner(NoKind)!.Value);
        Assert.Null(noKind.Kind);
        Assert.Equal((byte)77, noKind.Playback!.Priority);
        var auto = env.SoundDescriptor(env.Winner(Auto)!.Value);
        Assert.Equal(Comp, auto.BaseDescriptor);
        Assert.Equal(new SoundRateFile(1200, "sound\\fast.xwm"), Assert.Single(auto.RateFiles));
        Assert.Equal(Auto, env.SoundDescriptor(env.Winner(Comp)!.Value).Descriptors[1]);
        Assert.Equal(new byte[] { 0, 8, 0, 0 }, env.SoundDescriptor(env.Winner(Bare)!.Value).Controls);
        using var reloaded = PluginFile.Read(path);
        Assert.Equal(edited, reloaded.ToBytes());
    }

    [Fact]
    public void MarkerMusicAndFootstepEditsRespectCountsAndRequiredEntries()
    {
        using var temp = new TempDirectory();
        var path = temp.Write(Plugin, Source());
        using var plugin = PluginFile.Read(path);

        Assert.True(plugin.SetScalar(Marker, "SOUN\\RepeatMaxTime", "9"));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetScalar(Marker, "SOUN\\RepeatStackable", "2")));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.RemoveSubrecord(Marker, "SDSC")));
        Assert.True(plugin.SetScalar(Music, "MUSC\\Priority", "75"));
        Assert.Equal(2, plugin.FieldCount(Music, "MUSC\\Tracks"));
        Assert.True(plugin.SetFormLink(Music, "MUSC\\Tracks", null, 0));
        Assert.True(plugin.SetFormLink(Music, "MUSC\\Tracks", Must, 1));
        Assert.False(plugin.SetFormLink(Music, "MUSC\\Tracks", Must, 2));
        Assert.True(plugin.SetFormLink(Step, "FSTP\\ImpactDataSet", null));
        Assert.True(plugin.SetString(Step, "FSTP\\Tag", "FootRight"));
        Assert.Equal(5, plugin.FieldCount(Set, "FSTS\\Footsteps"));
        Assert.True(plugin.SetFormLink(Set, "FSTS\\Footsteps", Step2, 0));
        Assert.False(plugin.SetFormLink(Set, "FSTS\\Footsteps", Step2, 5));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetFormLink(Set, "FSTS\\Footsteps", null, 0)));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.RemoveSubrecord(Set, "DATA")));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.RemoveSubrecord(Set, "XCNT")));
        plugin.Write(path);

        using var env = RadicleEnvironment.Load(new[] { path });
        var marker = env.SoundMarker(env.Winner(Marker)!.Value);
        Assert.Equal(new SoundRepeat(1f, 9f, 1), marker.Repeat);
        var music = env.MusicType(env.Winner(Music)!.Value);
        Assert.Equal((ushort)75, music.Priority);
        Assert.Equal(new Radicle.Interop.FormKey?[] { null, Must }, music.Tracks);
        Assert.Equal(new Footstep(null, "FootRight"), env.Footstep(env.Winner(Step)!.Value));
        var set = env.FootstepSet(env.Winner(Set)!.Value);
        Assert.Equal(Step2, Assert.Single(set.Swimming));
        Assert.Equal(Step2, set.Running[1]);
    }

    [Fact]
    public void CreateBuildsDescriptorsMusicAndFootstepsAndRefusesMarkersAndSets()
    {
        using var temp = new TempDirectory();
        var path = Path.Combine(temp.Path, "Created", Plugin);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using (var plugin = PluginFile.Create(Plugin))
        {
            Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.CreateRecord("SOUN", "NewMarker")));
            Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.CreateRecord("FSTS", "NewSet")));
            var descriptor = plugin.CreateRecord("SNDR", "NewDescriptor");
            Assert.True(plugin.AddSubrecord(descriptor, "ANAM"));
            Assert.True(plugin.SetString(descriptor, "SNDR\\Sounds", "sound\\new.wav"));
            var music = plugin.CreateRecord("MUSC", "NewMusic");
            Assert.True(plugin.SetScalar(music, "MUSC\\FadeDuration", "2"));
            var step = plugin.CreateRecord("FSTP", "NewStep");
            Assert.True(plugin.SetString(step, "FSTP\\Tag", "Left"));
            plugin.Write(path);
        }

        using var env = RadicleEnvironment.Load(new[] { path });
        var created = env.SoundDescriptor(env.Winner(K(0x800))!.Value);
        Assert.Null(created.Kind);
        Assert.Equal(new[] { "sound\\new.wav" }, created.Files);
        var createdMusic = env.MusicType(env.Winner(K(0x801))!.Value);
        Assert.Equal((0u, (ushort)50, (ushort)0, 2f),
            (createdMusic.Flags!.Value, createdMusic.Priority!.Value, createdMusic.DuckingHundredths!.Value, createdMusic.FadeDuration!.Value));
        Assert.Empty(createdMusic.Tracks);
        Assert.Equal(new Footstep(null, "Left"), env.Footstep(env.Winner(K(0x802))!.Value));
    }
}
