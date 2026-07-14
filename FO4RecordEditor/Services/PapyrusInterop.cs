using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace FO4RecordEditor.Services;

// WebView2 host object for the Papyrus panel: compile .psc -> .pex (CK compiler) and decompile
// .pex -> .psc (our in-process decompiler). Same COM rules as AppInterop: AutoDual, ComVisible,
// only string/bool params and string/Task<string> returns. Native file pickers run on the UI thread
// (host-object calls are dispatched there); compile/decompile run on a background task so the long
// process call never blocks the UI.
[ClassInterface(ClassInterfaceType.AutoDual)]
[ComVisible(true)]
public class PapyrusInterop
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

    public Task<string> Compile(string source, string output, string imports, string flags,
        bool all, bool optimize, bool release, string compilerPath) =>
        Task.Run(() =>
        {
            try { return PapyrusService.Compile(source, N(output), N(imports), N(flags), all, optimize, release, N(compilerPath)); }
            catch (Exception ex) { DebugLog.Exception("Papyrus.Compile", ex); return "Error: " + ex.Message; }
        });

    public Task<string> Decompile(string source, string output, bool assembly, bool write) =>
        Task.Run(() =>
        {
            try { return PapyrusService.Decompile(source, N(output), assembly, write); }
            catch (Exception ex) { DebugLog.Exception("Papyrus.Decompile", ex); return "Error: " + ex.Message; }
        });

    /// <summary>Open a folder (or the folder containing a file) in Windows Explorer. Returns "" on success.</summary>
    public string OpenFolder(string path)
    {
        try
        {
            if (string.IsNullOrWhiteSpace(path)) return "No path.";
            path = path.Trim().Trim('"');
            if (File.Exists(path)) path = Path.GetDirectoryName(path) ?? path;
            if (!Directory.Exists(path)) return "Folder not found: " + path;
            Process.Start(new ProcessStartInfo { FileName = path, UseShellExecute = true });
            return "";
        }
        catch (Exception ex) { return "Error: " + ex.Message; }
    }

    /// <summary>Write a dropped file's bytes to a temp path and return that path, so drag-and-drop can
    /// set a usable source (WebView2 does not expose dropped files' real OS paths to JS). Returns the
    /// temp path, or a string starting with "ERR:" on failure.</summary>
    public string StageDroppedFile(string name, string base64)
    {
        try
        {
            var safe = Path.GetFileName(string.IsNullOrWhiteSpace(name) ? "dropped.bin" : name);
            var dir = Path.Combine(Path.GetTempPath(), "FO4RE_Drop_" + Guid.NewGuid().ToString("N").Substring(0, 8));
            Directory.CreateDirectory(dir);
            var path = Path.Combine(dir, safe);
            File.WriteAllBytes(path, Convert.FromBase64String(base64));
            return path;
        }
        catch (Exception ex) { return "ERR:" + ex.Message; }
    }

    private static string? N(string s) => string.IsNullOrWhiteSpace(s) ? null : s;
}
