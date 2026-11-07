using Godot;
using System;
using System.IO;
using System.Reflection;
using Radicle.Interop;
using OpenCommonwealth.Services.Terrain;
using OpenCommonwealth.Services.Water;
using OpenCommonwealth.Services.WaterBuild;

namespace OpenCommonwealth;

public partial class WaterSelectionProbe : Node
{
    public override void _Ready()
    {
        var data = System.Environment.GetEnvironmentVariable("FO4_DATA_DIR");
        if (string.IsNullOrEmpty(data))
        {
            GD.Print("water_selection_probe SKIP: FO4_DATA_DIR unset");
            GetTree().Quit(77);
            return;
        }
        try
        {
            using var env = RadicleEnvironment.Load(new[] { Path.Combine(data, "Fallout4.esm") });
            var world = env.FindEditorId("Commonwealth", "WRLD") ?? throw new InvalidOperationException("Commonwealth absent");
            var worldKey = env.Info(world).FormKey;
            var builder = new WaterBuilder(env, worldKey, new WorldHeightfield(-2048), "",
                new WaterBuilder.Options { IncludePlaced = false }, _ => { });
            var material = typeof(WaterBuilder).GetMethod("Material", BindingFlags.Instance | BindingFlags.NonPublic)
                ?? throw new InvalidOperationException("GardenEngine water converter absent");
            int cells = 0, heights = 0;
            foreach (var cell in env.WorldspaceCells(worldKey))
            {
                var selection = env.EffectiveWater(cell);
                if (selection.Record is not { } water) throw new InvalidOperationException("selected WATR unresolved");
                var converted = material.Invoke(builder, new object[] { selection.Water }) as WaterMaterial
                    ?? throw new InvalidOperationException("GardenEngine water conversion returned null");
                if (converted.FormKey != selection.Water.ToString() ||
                    converted.FogDepthAmount != env.Water(water).Visual?.FogDepthAmount)
                    throw new InvalidOperationException("selected material differs from WATR");
                ++cells;
                if (selection.Height is not null) ++heights;
            }
            if (cells == 0 || heights == 0) throw new InvalidOperationException("no exterior water selections");
            GD.Print($"water_selection_probe PASS cells={cells} heights={heights}");
            GetTree().Quit(0);
        }
        catch (Exception error)
        {
            GD.PrintErr("water_selection_probe FAIL: " + error);
            GetTree().Quit(1);
        }
    }
}
