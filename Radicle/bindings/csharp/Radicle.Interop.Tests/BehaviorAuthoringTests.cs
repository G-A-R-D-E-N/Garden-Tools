using System;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class BehaviorAuthoringTests
{
    [Fact]
    public void PackageDiscriminatedFieldsAndIdleStringsRoundTripThroughReaders()
    {
        using var temp = new TempDirectory();
        var input = temp.Write("Behavior.esp", Build(0, Array.Empty<string>(),
            Top("PACK", new Rec("PACK", 0x800, new[] { ("PKDT", new byte[12]), ("PSDT", new byte[12]),
                ("CNAM", U32(0x801)), ("PKCU", U32(1,0,9)), ("ANAM", Z("Integer")), ("CNAM", U32(0x802)),
                ("PLDT", U32(4,0x802)), ("PLDT", U32(6,0, uint.MaxValue)), ("PLDT", U32(0xAABBCC06,0,0)),
                ("PTDA", U32(4,33,0)), ("XNAM", Array.Empty<byte>()), ("POBA", Array.Empty<byte>()), ("INAM", U32(0)),
                ("POEA", Array.Empty<byte>()), ("INAM", U32(0)), ("POCA", Array.Empty<byte>()), ("INAM", U32(0)) })),
            Top("CSTY", new Rec("CSTY",0x801, new[] { ("DATA", U32(0)) })),
            Top("MISC", new Rec("MISC",0x802, Array.Empty<(string,byte[])>())),
            Top("IDLE", new Rec("IDLE",0x803, new[] { ("ANAM",new byte[8]), ("DATA",new byte[6]), ("ENAM",Z("event")) }))));
        var pack = new FormKey("Behavior.esp",0x800);
        var idle = new FormKey("Behavior.esp",0x803);
        var output = System.IO.Path.Combine(temp.Path,"out","Behavior.esp");
        using (var plugin = PluginFile.Read(input))
        {
            Assert.Equal(1,plugin.FieldCount(pack,"PACK\\CombatStyle"));
            Assert.True(plugin.SetScalar(pack,"PACK\\Month","-1"));
            Assert.True(plugin.SetScalar(pack,"PACK\\DurationMinutes","4294967295"));
            Assert.True(plugin.SetScalar(pack,"PACK\\LocationRadius","-2",1));
            Assert.True(plugin.SetScalar(pack,"PACK\\TargetAlias","-3"));
            Assert.True(plugin.SetFormLink(pack,"PACK\\LocationObject",null));
            Assert.True(plugin.SetFormLink(pack,"PACK\\Idles",idle,2));
            Assert.True(plugin.SetString(idle,"IDLE\\AnimationEvent","edited"));
            Assert.True(plugin.SetScalar(idle,"IDLE\\ReplayDelay","65535"));
            var before = plugin.ToBytes();
            Assert.False(plugin.SetScalar(pack,"PACK\\LocationRadius","1",0));
            Assert.False(plugin.SetScalar(pack,"PACK\\LocationRadius","1",2));
            Assert.Equal(Status.InvalidArgument,Assert.Throws<RadicleException>(() => plugin.RemoveSubrecord(pack,"PKCU")).Status);
            Assert.Equal(before,plugin.ToBytes());
            plugin.Write(output);
        }
        using var env = RadicleEnvironment.Load(new[] { output });
        var decoded = env.Package(env.Winner(pack)!.Value);
        Assert.Equal(uint.MaxValue,decoded.Schedule!.DurationMinutes); Assert.Equal((sbyte)-1,decoded.Schedule.Month);
        Assert.Equal(2,decoded.Locations.Count); Assert.Null(decoded.Locations[0].Radius); Assert.Null(decoded.Locations[0].Form);
        Assert.Equal(-2,decoded.Locations[1].Radius); Assert.Equal(-3,unchecked((int)decoded.Targets[0].Value));
        Assert.Equal(idle,decoded.ChangeIdle); Assert.Equal(U32(0x802),decoded.Opaque.Single(p => p.Signature=="CNAM").Bytes);
        var animation = env.Idle(env.Winner(idle)!.Value);
        Assert.Equal("edited",animation.AnimationEvent); Assert.Equal((ushort)65535,animation.Data!.ReplayDelay);
    }

    [Fact]
    public void CreateDefaultsAndOptionalStringAddRemoveArePublic()
    {
        using var plugin = PluginFile.Create("Behavior.esp");
        var style = plugin.CreateRecord("CSTY","NewStyle");
        Assert.True(plugin.SetScalar(style,"CSTY\\OffensiveMult","0.75"));
        var idle = plugin.CreateRecord("IDLE","NewIdle");
        Assert.True(plugin.AddSubrecord(idle,"DNAM")); Assert.True(plugin.SetString(idle,"IDLE\\BehaviorGraph","new.hkx"));
        Assert.True(plugin.RemoveSubrecord(idle,"DNAM"));
        Assert.Equal(Status.InvalidArgument,Assert.Throws<RadicleException>(() => plugin.CreateRecord("PACK","Refused")).Status);
        Assert.Equal(Status.InvalidArgument,Assert.Throws<RadicleException>(() => plugin.RemoveSubrecord(style,"CSGD")).Status);
    }
}
