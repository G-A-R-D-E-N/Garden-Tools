using System.IO;
using Newtonsoft.Json;

namespace FO4RecordEditor.Services;

public sealed class SettingsService
{
    private static readonly string Dir =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                     "FO4RecordEditor");
    private static readonly string FilePath = Path.Combine(Dir, "settings.json");

    public AppSettings Current { get; private set; } = new();

    public void Load()
    {
        try
        {
            if (File.Exists(FilePath))
                Current = JsonConvert.DeserializeObject<AppSettings>(File.ReadAllText(FilePath))
                          ?? new AppSettings();
        }
        catch { Current = new AppSettings(); }
    }

    public void Save()
    {
        Directory.CreateDirectory(Dir);
        File.WriteAllText(FilePath, JsonConvert.SerializeObject(Current, Formatting.Indented));
    }
}
