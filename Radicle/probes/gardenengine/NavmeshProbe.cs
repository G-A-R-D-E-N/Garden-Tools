using Godot;
using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using Radicle.Interop;

namespace OpenCommonwealth;

public partial class NavmeshProbe : Node
{
    public override void _Ready()
    {
        var data = System.Environment.GetEnvironmentVariable("FO4_DATA_DIR");
        if (string.IsNullOrEmpty(data))
        {
            GD.Print("navmesh_probe SKIP: FO4_DATA_DIR unset");
            GetTree().Quit(77);
            return;
        }
        try
        {
            using var env = RadicleEnvironment.Load(new[] { Path.Combine(data, "Fallout4.esm") });
            var navi = env.WinnersOf("NAVI");
            if (navi.Count != 1) throw new InvalidOperationException("NAVI record count is " + navi.Count + ", expected 1");
            var infos = env.NavmeshInfos(navi[0]);
            if (infos.Count == 0) throw new InvalidOperationException("NAVI has no NVMI entries");
            int navmeshLinks = 0, navmeshUnresolved = 0, worldLinks = 0, worldUnresolved = 0, cellLinks = 0, cellUnresolved = 0;
            foreach (var info in infos)
            {
                Tally(env, info.Navmesh, ref navmeshLinks, ref navmeshUnresolved);
                Tally(env, info.Parent.World, ref worldLinks, ref worldUnresolved);
                Tally(env, info.Parent.Cell, ref cellLinks, ref cellUnresolved);
            }
            RequireSomeResolved(navmeshLinks, navmeshUnresolved, "NVMI navmesh");
            RequireSomeResolved(worldLinks, worldUnresolved, "NVMI parent world");
            RequireSomeResolved(cellLinks, cellUnresolved, "NVMI parent cell");
            var (rec, geometry) = env.WinnersOf("NAVM").Select(r => (Rec: r, Mesh: env.Navmesh(r)))
                .FirstOrDefault(x => x.Mesh is { Vertices.Count: > 0, Triangles.Count: > 0 });
            var mesh = geometry ?? throw new InvalidOperationException("no NAVM decodes with vertices and triangles");
            foreach (var triangle in mesh.Triangles)
                foreach (var vertex in triangle.Vertices)
                    if (vertex >= mesh.Vertices.Count)
                        throw new InvalidOperationException("NAVM triangle vertex " + vertex + " outside " + mesh.Vertices.Count + " vertices");
            RequireResolved(env, mesh.Parent.World, "NAVM parent world");
            RequireResolved(env, mesh.Parent.Cell, "NAVM parent cell");
            GD.Print("navmesh_probe PASS " + JsonSerializer.Serialize(new
            {
                Navi = env.Info(navi[0]).FormKey.ToString(), NaviInfos = infos.Count,
                Navmesh = env.Info(rec).FormKey.ToString(), mesh.Version, Vertices = mesh.Vertices.Count,
                Triangles = mesh.Triangles.Count, World = mesh.Parent.World?.ToString(), Cell = mesh.Parent.Cell?.ToString(),
                NavmeshLinks = navmeshLinks, NavmeshUnresolved = navmeshUnresolved,
                WorldLinks = worldLinks, WorldUnresolved = worldUnresolved,
                CellLinks = cellLinks, CellUnresolved = cellUnresolved,
            }));
            GetTree().Quit(0);
        }
        catch (Exception error)
        {
            GD.PrintErr("navmesh_probe FAIL: " + error);
            GetTree().Quit(1);
        }
    }

    private static void Tally(RadicleEnvironment env, FormKey? key, ref int links, ref int unresolved)
    {
        if (key is not { } link) return;
        links++;
        if (env.Winner(link) is null) unresolved++;
    }

    private static void RequireSomeResolved(int links, int unresolved, string what)
    {
        if (links > 0 && unresolved == links)
            throw new InvalidOperationException(what + " links all unresolved: " + unresolved + " of " + links);
    }

    private static void RequireResolved(RadicleEnvironment env, FormKey? key, string what)
    {
        if (key is { } link && env.Winner(link) is null)
            throw new InvalidOperationException(what + " unresolved: " + link);
    }
}
