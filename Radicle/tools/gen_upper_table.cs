#:property TargetFramework=net8.0
using System.Text;

var runs = new List<int[]>();
for (int cp = 0x80; cp <= 0x10FFFF; cp++)
{
    if (cp >= 0xD800 && cp <= 0xDFFF) continue;
    int upper = Rune.ToUpperInvariant(new Rune(cp)).Value;
    if (upper == cp) continue;
    if (!string.Equals(char.ConvertFromUtf32(cp), char.ConvertFromUtf32(upper), StringComparison.OrdinalIgnoreCase)) continue;
    int delta = upper - cp;
    if (runs.Count > 0)
    {
        var last = runs[^1];
        int gap = cp - last[1];
        if (last[3] == delta && (gap == 1 || gap == 2) && (last[0] == last[1] || last[2] == gap))
        {
            last[1] = cp;
            last[2] = gap;
            continue;
        }
    }
    runs.Add(new[] { cp, cp, 1, delta });
}

var text = new StringBuilder();
foreach (var run in runs) text.Append($"{{0x{run[0]:X4}, 0x{run[1]:X4}, {run[2]}, {run[3]}}},\n");
File.WriteAllText(args[0], text.ToString());
Console.WriteLine($"{runs.Count} runs from .NET {Environment.Version}");
