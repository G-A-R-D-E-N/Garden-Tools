using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace FO4RecordEditor.Services;

// WebView2 host object for the Mod Catalog panel: summarize an unfamiliar mod folder's contents by
// category via ModInspectService. Same COM rules as ArchiveInterop/PapyrusInterop: AutoDual,
// ComVisible, Task<string> return so a big folder walk never blocks the UI thread.
[ClassInterface(ClassInterfaceType.AutoDual)]
[ComVisible(true)]
public class ModCatalogInterop
{
    public string BrowseForFolder(string title)
    {
        var dlg = new Microsoft.Win32.OpenFolderDialog
        {
            Title = string.IsNullOrWhiteSpace(title) ? "Select a mod folder" : title,
        };
        return dlg.ShowDialog() == true ? dlg.FolderName : "";
    }

    public Task<string> Catalog(string modPath) =>
        Task.Run(() =>
        {
            try { return ModInspectService.CatalogFolder(modPath); }
            catch (Exception ex) { DebugLog.Exception("ModCatalog.Catalog", ex); return "Error: " + ex.Message; }
        });

    /// <summary>Open a folder in Windows Explorer. Returns "" on success.</summary>
    public string OpenFolder(string path)
    {
        try
        {
            if (string.IsNullOrWhiteSpace(path)) return "No path.";
            path = path.Trim().Trim('"');
            if (!Directory.Exists(path)) return "Folder not found: " + path;
            Process.Start(new ProcessStartInfo { FileName = path, UseShellExecute = true });
            return "";
        }
        catch (Exception ex) { return "Error: " + ex.Message; }
    }
}
