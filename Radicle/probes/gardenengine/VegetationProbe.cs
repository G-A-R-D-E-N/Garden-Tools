using Godot;
using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using Radicle.Interop;

namespace OpenCommonwealth;

public partial class VegetationProbe : Node
{
    public override void _Ready()
    {
        var data = System.Environment.GetEnvironmentVariable("FO4_DATA_DIR");
        if (string.IsNullOrEmpty(data))
        {
            GD.Print("vegetation_probe SKIP: FO4_DATA_DIR unset");
            GetTree().Quit(77);
            return;
        }
        try
        {
            using var env = RadicleEnvironment.Load(new[] { Path.Combine(data, "Fallout4.esm") });
            var grassRecord = env.WinnersOf("GRAS").FirstOrDefault(r => env.Grass(r) is not null);
            if (grassRecord.Handle == IntPtr.Zero) throw new InvalidOperationException("no vanilla GRAS with DATA");
            var grass = env.Grass(grassRecord)!;
            var treeRecord = env.WinnersOf("TREE").FirstOrDefault();
            if (treeRecord.Handle == IntPtr.Zero) throw new InvalidOperationException("no vanilla TREE");
            var tree = env.Tree(treeRecord);
            if (tree.Wind is null || tree.Wind.Length != 12) throw new InvalidOperationException("TREE CNAM wind absent or not twelve values");
            var floraRecord = env.WinnersOf("FLOR").FirstOrDefault(r => env.Flora(r).Flags is not null);
            if (floraRecord.Handle == IntPtr.Zero) throw new InvalidOperationException("no vanilla FLOR with FNAM");
            var flora = env.Flora(floraRecord);
            var ltexRecord = env.WinnersOf("LTEX").FirstOrDefault(r => env.LandscapeGrasses(r).Count > 0);
            if (ltexRecord.Handle == IntPtr.Zero) throw new InvalidOperationException("no vanilla LTEX with GNAM");
            var grasses = env.LandscapeGrasses(ltexRecord).Select(link =>
            {
                var key = link ?? throw new InvalidOperationException("LTEX GNAM link is null");
                var winner = env.Winner(key) ?? throw new InvalidOperationException("LTEX GNAM " + key + " unresolved");
                if (env.Info(winner).Signature != "GRAS") throw new InvalidOperationException("LTEX GNAM " + key + " is not GRAS");
                return key.ToString();
            }).ToArray();
            GD.Print("vegetation_probe PASS " + JsonSerializer.Serialize(new
            {
                Grass = env.Info(grassRecord).EditorId, grass.Density, grass.MaxSlope,
                Tree = env.Info(treeRecord).EditorId, WindValues = tree.Wind.Length,
                Flora = env.Info(floraRecord).EditorId, flora.Flags,
                LandscapeTexture = env.Info(ltexRecord).EditorId, LandscapeGrasses = grasses,
            }));
            GetTree().Quit(0);
        }
        catch (Exception error)
        {
            GD.PrintErr("vegetation_probe FAIL: " + error);
            GetTree().Quit(1);
        }
    }
}
