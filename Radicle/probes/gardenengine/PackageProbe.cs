using Godot;
using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using Radicle.Interop;

namespace OpenCommonwealth;

public partial class PackageProbe : Node
{
    public override void _Ready()
    {
        var data = System.Environment.GetEnvironmentVariable("FO4_DATA_DIR");
        if (string.IsNullOrEmpty(data))
        {
            GD.Print("package_probe SKIP: FO4_DATA_DIR unset");
            GetTree().Quit(77);
            return;
        }
        try
        {
            using var env = RadicleEnvironment.Load(new[] { Path.Combine(data, "Fallout4.esm") });
            var (pack, package) = env.WinnersOf("PACK")
                .Select(p => (Pack: p, Package: env.Package(p)))
                .Where(x => IsComplete(env, x.Package))
                .Cast<(Record Pack, Package Package)?>().FirstOrDefault()
                ?? throw new InvalidOperationException("no PACK in Fallout4.esm has data, schedule, a resolvable template and a resolvable location or target");
            var styleRecord = env.WinnersOf("CSTY").Cast<Record?>().FirstOrDefault()
                ?? throw new InvalidOperationException("no CSTY in Fallout4.esm");
            var procedure = env.PackageProcedure(pack);
            var combatStyle = env.CombatStyle(styleRecord);
            var idleRecord = env.WinnersOf("IDLE").Cast<Record?>().FirstOrDefault()
                ?? throw new InvalidOperationException("no IDLE in Fallout4.esm");
            var idle = env.Idle(idleRecord);
            if (!LinksResolve(env, idle.Parent, idle.Previous)) throw new InvalidOperationException("IDLE parent or previous link does not resolve");
            GD.Print("package_probe PASS " + JsonSerializer.Serialize(new
            {
                Pack = env.Info(pack).FormKey.ToString(), PackageDataType = package.Data?.Type,
                Template = package.Counter?.TemplatePackage?.ToString(), Locations = package.Locations.Count,
                Targets = package.Targets.Count, Conditions = package.Conditions.Count, Opaque = package.Opaque.Count,
                RootProcedure = procedure.Nodes.Count == 0 ? null : procedure.Nodes[0].Name,
                LeafProcedures = procedure.Nodes.Where(node => node.IsLeaf).Select(node => node.Name).ToArray(),
                CombatStyle = env.Info(styleRecord).FormKey.ToString(), CombatStyleMelee = combatStyle.Melee.Length,
                Idle = env.Info(idleRecord).FormKey.ToString(),
            }));
            GetTree().Quit(0);
        }
        catch (Exception error)
        {
            GD.PrintErr("package_probe FAIL: " + error);
            GetTree().Quit(1);
        }
    }

    private static bool IsComplete(RadicleEnvironment env, Package package) =>
        package.Data is not null && package.Schedule is not null && Resolves(env, package.Counter?.TemplatePackage)
        && (package.Locations.Any(location => Resolves(env, location.Form)) || package.Targets.Any(target => Resolves(env, target.Form)));

    private static bool Resolves(RadicleEnvironment env, FormKey? key) => key is { } k && env.Winner(k) is not null;

    private static bool LinksResolve(RadicleEnvironment env, params FormKey?[] links) =>
        links.All(link => link is not { } key || env.Winner(key) is not null);
}
