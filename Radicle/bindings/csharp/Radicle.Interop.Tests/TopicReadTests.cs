using System;
using System.Linq;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class TopicReadTests
{
    private static Rec Record(string signature, uint id, string name) => new(signature, id, new[] { ("EDID", Z(name)) });

    [Fact]
    public void SelectedTopicKeepsPhysicalChildVersionsAndOrder()
    {
        using var temp = new TempDirectory();
        var master = temp.Write("Master.esm", Build(1, Array.Empty<string>(), Top("DIAL",
            Record("DIAL", 0x800, "Topic"), new Grp(0x800, 7, new Node[]
            {
                Record("INFO", 0x802, "SecondIdFirst"), Record("INFO", 0x801, "FirstIdSecond"),
            }), Record("DIAL", 0x803, "Sibling"), new Grp(0x803, 7, new Node[] { Record("INFO", 0x804, "SiblingLine") }))));
        var patch = temp.Write("Patch.esp", Build(0, new[] { "Master.esm" }, Top("DIAL",
            new Grp(0x800, 7, new Node[] { Record("INFO", 0x802, "WinningOverride") }))));
        var selected = temp.Write("Selected.esp", Build(0, new[] { "Master.esm", "Patch.esp" },
            Top("QUST", Record("QUST", 0x02000810, "Quest"), new Grp(0x02000810, 10, new Node[]
            {
                Top("DIAL", Record("DIAL", 0x800, "SelectedTopic"), new Grp(0x800, 7, new Node[]
                {
                    Record("INFO", 0x02000805, "OwnLine"), Record("INFO", 0x801, "SelectedLine"),
                })),
            }))));
        using var env = RadicleEnvironment.Load(new[] { master, patch, selected });
        var versions = env.Versions(new FormKey("Master.esm", 0x800));
        var original = env.TopicChildren(versions[0]);
        Assert.Equal(new[] { "SecondIdFirst", "FirstIdSecond" }, original.Select(r => env.Info(r).EditorId));
        Assert.All(original, r => Assert.Equal(0, env.Info(r).PluginIndex));
        Assert.Equal("WinningOverride", env.Info(env.Winner(new FormKey("Master.esm", 0x802))!.Value).EditorId);
        var children = env.TopicChildren(versions[1]);
        Assert.Equal(new[] { "OwnLine", "SelectedLine" }, children.Select(r => env.Info(r).EditorId));
        Assert.All(children, r => Assert.Equal(2, env.Info(r).PluginIndex));
        Assert.Equal(new FormKey("Selected.esp", 0x805), env.Info(children[0]).FormKey);
        Assert.Equal("SiblingLine", env.Info(Assert.Single(env.TopicChildren(env.Winner(new FormKey("Master.esm", 0x803))!.Value))).EditorId);
        Assert.Throws<ArgumentException>(() => env.TopicChildren(default));
        Assert.Throws<RadicleException>(() => env.TopicChildren(children[0]));
        env.Dispose();
        Assert.Throws<ObjectDisposedException>(() => env.TopicChildren(versions[0]));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(2)]
    public void MissingEmptyOrDifferentFollowingGroupIsEmpty(int kind)
    {
        using var temp = new TempDirectory();
        Node[] nodes = kind == 0 ? new Node[] { Record("DIAL", 0x800, "Topic") }
            : new Node[] { Record("DIAL", 0x800, "Topic"), new Grp(0x800, kind == 1 ? 7 : 0, Array.Empty<Node>()) };
        var path = temp.Write("Empty.esp", Build(0, Array.Empty<string>(), Top("DIAL", nodes)));
        using var env = RadicleEnvironment.Load(new[] { path });
        Assert.Empty(env.TopicChildren(env.Winner(new FormKey("Empty.esp", 0x800))!.Value));
    }

    [Theory]
    [InlineData(0x801u)]
    [InlineData(0u)]
    [InlineData(0xffffffffu)]
    [InlineData(0x01000800u)]
    [InlineData(0xff000800u)]
    public void InvalidOrMismatchedGroupOwnerIsRejected(uint label)
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Bad.esp", Build(0, Array.Empty<string>(), Top("DIAL",
            Record("DIAL", 0x800, "Topic"), new Grp(label, 7, Array.Empty<Node>()))));
        using var env = RadicleEnvironment.Load(new[] { path });
        Assert.Throws<RadicleException>(() => env.TopicChildren(env.Winner(new FormKey("Bad.esp", 0x800))!.Value));
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void NonInfoOrNestedChildIsRejected(bool nested)
    {
        using var temp = new TempDirectory();
        Node child = nested ? Top("INFO", Record("INFO", 0x801, "Nested")) : Record("BOOK", 0x801, "WrongType");
        var path = temp.Write("Bad.esp", Build(0, Array.Empty<string>(), Top("DIAL",
            Record("DIAL", 0x800, "Topic"), new Grp(0x800, 7, new[] { child }))));
        using var env = RadicleEnvironment.Load(new[] { path });
        Assert.Throws<RadicleException>(() => env.TopicChildren(env.Winner(new FormKey("Bad.esp", 0x800))!.Value));
    }
}
