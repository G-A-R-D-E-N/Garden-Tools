using System;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using FO4RecordEditor.ViewModels;
using Newtonsoft.Json;

namespace FO4RecordEditor.Services;

// WebView2 host object for the Cell Viewer panel: read a CELL's placed references (CellService) and
// batch-resolve+convert their meshes to geometry (NifService.GeoBatch). Reads the env from the shell
// on each call (MastersInterop's pattern) so it always reflects the currently loaded modlist.
// Task<string> returns so a busy cell's reference union / a batch of niftool conversions never block
// the UI thread.
[ClassInterface(ClassInterfaceType.AutoDual)]
[ComVisible(true)]
public class CellInterop
{
    private readonly ShellViewModel _shell;
    public CellInterop(ShellViewModel shell) => _shell = shell;
    private object? Env => _shell.GameEnvironment;

    /// <summary>Placed-reference list (position/rotation/scale/model path) for a cell, unioned across
    /// the whole load order. cellId: a FormKey ("001234:Fallout4.esm") or an EditorID. See
    /// CellService.GetPlacedReferencesJson's doc comment for the union-not-override semantics.</summary>
    public Task<string> GetPlacedReferences(string cellId) =>
        Task.Run(() =>
        {
            try { return CellService.GetPlacedReferencesJson(cellId, Env); }
            catch (Exception ex)
            {
                DebugLog.Exception("Cell.GetPlacedReferences", ex);
                return JsonConvert.SerializeObject(new { error = "Error: " + ex.Message });
            }
        });

    /// <summary>Batch-resolve+convert unique Data-relative NIF paths (from GetPlacedReferences'
    /// modelPath fields) to geometry JSON, one niftool call per unique mesh. relModelPathsJson: a
    /// JSON array of strings. See NifService.GeoBatch.</summary>
    public Task<string> GetGeometryBatch(string relModelPathsJson) =>
        Task.Run(() =>
        {
            try
            {
                var paths = JsonConvert.DeserializeObject<string[]>(relModelPathsJson) ?? Array.Empty<string>();
                return NifService.GeoBatch(paths);
            }
            catch (Exception ex)
            {
                DebugLog.Exception("Cell.GetGeometryBatch", ex);
                return JsonConvert.SerializeObject(new { error = "Error: " + ex.Message });
            }
        });
}
