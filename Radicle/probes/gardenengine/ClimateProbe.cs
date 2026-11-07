using Godot;
using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text.Json;
using Radicle.Interop;
using OpenCommonwealth.Services;

namespace OpenCommonwealth;

public partial class ClimateProbe : Node
{
    public override void _Ready()
    {
        var data = System.Environment.GetEnvironmentVariable("FO4_DATA_DIR");
        if (string.IsNullOrEmpty(data))
        {
            GD.Print("climate_probe SKIP: FO4_DATA_DIR unset");
            GetTree().Quit(77);
            return;
        }
        try
        {
            using var env = RadicleEnvironment.Load(new[] { Path.Combine(data, "Fallout4.esm") });
            var world = env.FindEditorId("Commonwealth", "WRLD") ?? throw new InvalidOperationException("Commonwealth absent");
            var climateKey = env.Worldspace(world).Climate ?? throw new InvalidOperationException("world climate absent");
            var climateRecord = env.Winner(climateKey) ?? throw new InvalidOperationException("climate unresolved");
            var climate = env.Climate(climateRecord);
            if (climate.Timing is null) throw new InvalidOperationException("timing absent");
            var weatherKey = climate.Weathers.OrderByDescending(w => w.Chance).First().Weather
                ?? throw new InvalidOperationException("weather link absent");
            var weatherRecord = env.Winner(weatherKey) ?? throw new InvalidOperationException("weather unresolved");
            var weather = env.Weather(weatherRecord);
            if (weather.Parameters is null) throw new InvalidOperationException("parameters absent");
            var convert = typeof(CellService).GetMethod("BuildExteriorSkyJson", BindingFlags.Static | BindingFlags.NonPublic)
                ?? throw new InvalidOperationException("GardenEngine sky converter absent");
            var sky = convert.Invoke(null, new object[] { env, world })
                ?? throw new InvalidOperationException("GardenEngine sky conversion returned null");
            GD.Print("climate_probe PASS " + JsonSerializer.Serialize(new
            {
                World = env.Info(world).FormKey.ToString(), Climate = climateKey.ToString(), Weather = weatherKey.ToString(),
                climate.Timing, weather.Parameters, Sky = sky,
            }));
            GetTree().Quit(0);
        }
        catch (Exception error)
        {
            GD.PrintErr("climate_probe FAIL: " + error);
            GetTree().Quit(1);
        }
    }
}
