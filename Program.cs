using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Fallout4;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Binary.Parameters;
using System.Text.RegularExpressions;

const double FACTOR = 0.5;
string srcPath  = @"D:\Games\ModlistDownloads\mods\Backpacks of the Commonwealth\Backpacks of the Commonwealth.esp";
string patchDir = @"D:\Games\ModlistDownloads\mods\FW Backpacks Rebalance\";
string patchPath= patchDir + "FW_Backpacks_Rebalance.esp";
Directory.CreateDirectory(patchDir);

var orig = Fallout4Mod.CreateFromBinary(ModPath.FromPath(srcPath), Fallout4Release.Fallout4);
var bpKey = orig.ModKey;
var CW  = FormKey.Factory("0002DC:Fallout4.esm");
var MIR = new FormKey(bpKey, 0x00D3FA);
var patch = new Fallout4Mod(ModKey.FromNameAndExtension("FW_Backpacks_Rebalance.esp"), Fallout4Release.Fallout4);

var rxName = new Regex(@"CC(\d+)");
var rxDesc = new Regex(@"capacity by \+(\d+)");
int n=0, fb=0; var report=new List<string>();

void SetCW(IArmorModification am, float val){
    foreach (var p in am.Properties)
        if (p is ObjectModFormLinkFloatProperty<Armor.Property> fp && (fp.Record.FormKey==CW||fp.Record.FormKey==MIR))
            fp.Value = val;
}

foreach (var omod in orig.ObjectModifications.ToList())
{
    var ed = omod.EditorID ?? ""; if (!rxName.IsMatch(ed)) continue;
    if (omod is not IArmorModification am) continue;
    string d = am.Description?.String ?? "";
    var md = rxDesc.Match(d);
    int V; bool fromDesc = md.Success;
    if (fromDesc) V = int.Parse(md.Groups[1].Value);
    else { V = int.Parse(rxName.Match(ed).Groups[1].Value); fb++; }
    int H = (int)Math.Round(V*FACTOR, MidpointRounding.AwayFromZero); if (H<1) H=1;

    // patch override: halved CW + fixed desc
    var ov = (IArmorModification)patch.ObjectModifications.GetOrAddAsOverride(omod);
    SetCW(ov, H);
    if (ov.Description != null && fromDesc) ov.Description = d.Replace($"+{V}", $"+{H}");

    // revert original to true value V
    SetCW(am, V);
    report.Add($"{ed}: V={V}{(fromDesc?"":"(name)")} -> patch {H}");
    n++;
}

// verify original record counts unchanged vs on-disk overlay
var diskCounts = new Dictionary<string,int>();
var disk = Fallout4Mod.CreateFromBinaryOverlay(srcPath, Fallout4Release.Fallout4);
int diskTotal=0, memTotal=0;
foreach (var r in disk.EnumerateMajorRecords()) diskTotal++;
foreach (var r in orig.EnumerateMajorRecords()) memTotal++;

var prms = new BinaryWriteParameters { MastersListContent = MastersListContentOption.Iterate, ModKey = ModKeyOption.NoCheck };
patch.WriteToBinary(patchPath, prms);

string origTmp = srcPath + ".revtmp";
orig.WriteToBinary(origTmp, prms);

Console.WriteLine($"CC OMODs processed: {n} | name-fallback: {fb}");
Console.WriteLine($"record count disk(old)={diskTotal} mem(reverted)={memTotal} {(diskTotal==memTotal?"OK":"MISMATCH!")}");
Console.WriteLine($"patch written: {patchPath}");
Console.WriteLine($"reverted original staged: {origTmp}");
foreach (var r in report.Take(8)) Console.WriteLine("   "+r);
