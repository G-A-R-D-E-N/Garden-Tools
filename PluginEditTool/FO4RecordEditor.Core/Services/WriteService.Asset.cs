using System;
using System.IO;

namespace FO4RecordEditor.Services;

public static partial class WriteService
{
    // Constraint: the first backup must survive later saves, and the new bytes land through a temp file moved over the target.
    public static string SaveAsset(string path, byte[] bytes)
    {
        var backup = path + ".bak";
        if (File.Exists(path) && !File.Exists(backup)) File.Copy(path, backup);
        var temp = Path.Combine(Path.GetDirectoryName(Path.GetFullPath(path)) ?? ".",
            "." + Path.GetFileName(path) + "." + Guid.NewGuid().ToString("N") + ".tmp");
        try
        {
            File.WriteAllBytes(temp, bytes);
            File.Move(temp, path, overwrite: true);
        }
        finally
        {
            if (File.Exists(temp)) File.Delete(temp);
        }
        return path;
    }
}
