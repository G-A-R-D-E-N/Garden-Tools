using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Newtonsoft.Json;

namespace FO4RecordEditor.Services;

// WebView2 host object for the Archive panel: list/extract BA2/BSA contents via ArchiveService.
// Same COM rules as NifInterop/MaterialInterop: AutoDual, ComVisible, Task<string> returns so a big
// archive scan/extract never blocks the UI thread.
[ClassInterface(ClassInterfaceType.AutoDual)]
[ComVisible(true)]
public class ArchiveInterop
{
    public string BrowseForFile(string title, string filter)
    {
        var dlg = new Microsoft.Win32.OpenFileDialog
        {
            Title = string.IsNullOrWhiteSpace(title) ? "Select a file" : title,
            Filter = string.IsNullOrWhiteSpace(filter) ? "All files|*.*" : filter,
        };
        return dlg.ShowDialog() == true ? dlg.FileName : "";
    }

    public string BrowseForFolder(string title)
    {
        var dlg = new Microsoft.Win32.OpenFolderDialog
        {
            Title = string.IsNullOrWhiteSpace(title) ? "Select a folder" : title,
        };
        return dlg.ShowDialog() == true ? dlg.FolderName : "";
    }

    /// <summary>Structured entry list (JSON) for the panel's table. See ArchiveService.ListArchiveJson.</summary>
    public Task<string> List(string archivePath, string filter, int limit) =>
        Task.Run(() =>
        {
            try { return ArchiveService.ListArchiveJson(archivePath, string.IsNullOrWhiteSpace(filter) ? null : filter, limit); }
            catch (Exception ex)
            {
                DebugLog.Exception("Archive.List", ex);
                return JsonConvert.SerializeObject(new { error = "Error: " + ex.Message });
            }
        });

    public Task<string> ExtractFile(string archivePath, string innerPath, string outPath) =>
        Task.Run(() =>
        {
            try { return ArchiveService.ExtractFile(archivePath, innerPath, outPath); }
            catch (Exception ex) { DebugLog.Exception("Archive.ExtractFile", ex); return "Error: " + ex.Message; }
        });

    /// <summary>innerPathsJson: a JSON array of the panel's selected entry paths.</summary>
    public Task<string> ExtractSelected(string archivePath, string innerPathsJson, string outDir) =>
        Task.Run(() =>
        {
            try
            {
                var paths = JsonConvert.DeserializeObject<List<string>>(innerPathsJson) ?? new List<string>();
                return ArchiveService.ExtractSelected(archivePath, paths, outDir);
            }
            catch (Exception ex) { DebugLog.Exception("Archive.ExtractSelected", ex); return "Error: " + ex.Message; }
        });

    public Task<string> ExtractAll(string archivePath, string outDir, string filter, int limit) =>
        Task.Run(() =>
        {
            try { return ArchiveService.ExtractAll(archivePath, outDir, string.IsNullOrWhiteSpace(filter) ? null : filter, limit); }
            catch (Exception ex) { DebugLog.Exception("Archive.ExtractAll", ex); return "Error: " + ex.Message; }
        });

    /// <summary>Open a folder (or the folder containing a file) in Windows Explorer. Returns "" on success.</summary>
    public string OpenFolder(string path)
    {
        try
        {
            if (string.IsNullOrWhiteSpace(path)) return "No path.";
            path = path.Trim().Trim('"');
            if (System.IO.File.Exists(path)) path = System.IO.Path.GetDirectoryName(path) ?? path;
            if (!System.IO.Directory.Exists(path)) return "Folder not found: " + path;
            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo { FileName = path, UseShellExecute = true });
            return "";
        }
        catch (Exception ex) { return "Error: " + ex.Message; }
    }
}
