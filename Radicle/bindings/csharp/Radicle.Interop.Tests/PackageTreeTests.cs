using System;
using System.IO;
using System.Linq;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class PackageTreeTests
{
    private const string Plugin = "Pack.esp";
    private static Radicle.Interop.FormKey K(uint id) => new(Plugin, id);

    private static byte[] B(params byte[][] parts) => parts.SelectMany(p => p).ToArray();

    private static byte[] Source()
    {
        return Build(0, Array.Empty<string>(),
            Top("PACK", new Rec("PACK", 0x800, new[]
            {
                ("PKCU", B(U32(0), U32(0), U32(1))),
                ("ANAM", Z("Sequence")),
                ("CITC", U32(0)),
                ("PRCB", B(U32(1), U32(0))),
                ("ANAM", Z("Procedure")),
                ("CITC", U32(0)),
                ("PNAM", Z("Activate")),
                ("POBA", Array.Empty<byte>()),
            })));
    }

    [Fact]
    public void AddAndRemoveProcedureNodesRecomputeTheBranchCount()
    {
        using var temp = new TempDirectory();
        using var plugin = PluginFile.Read(temp.Write("Pack.esp", Source()));
        var key = K(0x800);
        var added = plugin.AddPackageProcedure(key, 0, "Wander");
        Assert.True(added > 0);
        var path = Path.Combine(temp.Path, "Saved.esp");
        plugin.Write(path);
        using var env = RadicleEnvironment.Load(new[] { path });
        var record = env.Winner(new Radicle.Interop.FormKey("Saved.esp", 0x800))!.Value;
        var tree = env.PackageProcedure(record);
        Assert.True(tree.Decoded);
        Assert.Equal(3, tree.Nodes.Count);
        Assert.Equal(2u, tree.Nodes[0].ChildCount);
        Assert.Equal("Wander", tree.Nodes[added].Name);
        plugin.RemovePackageProcedure(key, added);
        Assert.Throws<RadicleException>(() => plugin.AddPackageProcedure(key, 0, "Fly"));
        Assert.Throws<RadicleException>(() => plugin.AddPackageProcedure(key, 1, "Activate"));
        Assert.Throws<RadicleException>(() => plugin.RemovePackageProcedure(key, 0));
        Assert.Throws<RadicleException>(() => plugin.RemovePackageProcedure(key, 9));
    }
}
