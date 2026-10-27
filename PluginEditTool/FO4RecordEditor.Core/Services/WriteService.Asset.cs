using System.IO;

namespace FO4RecordEditor.Services;

public static partial class WriteService
{
    // Constraint: an asset write must back up the previous bytes before the first new byte lands.
    public static string SaveAsset(string path, byte[] bytes)
    {
        if (File.Exists(path))
        {
            File.Copy(path, path + ".bak", overwrite: true);
        }
        File.WriteAllBytes(path, bytes);
        return path;
    }
}